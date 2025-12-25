//负责和VRAM交互进行读写。也就是说从指定的内存地址，例如0x8100_0000读
//对于标准的VLE64/VSE64，直接直通连接。
//对于VLX/VSX，需要进行宽度转换和扩展/截断。

`include "v_defines.v"

module v_mem (
    input                      clk,
    input                      rst,
   
    input                      vmem_ren_i,
    input                      vmem_wen_i,
    input   [`VMEM_ADDR_BUS]   vmem_addr_i,
    input   [`VMEM_DATA_BUS]   vmem_din_i,
    input   [2:0]              vmem_width_i,    // VLX/VSX的width字段
    input   [2:0]              vmem_len_i,      // VLX/VSX的len字段
    input                      vmem_is_vlx_i,   // 是否为VLX指令
    input                      vmem_is_vsx_i,   // 是否为VSX指令
    output  [`VMEM_DATA_BUS]   vmem_dout_o,

    output                     vram_ren_o,
    output                     vram_wen_o,
    output  [`VRAM_ADDR_BUS]   vram_addr_o,
    output  [`VRAM_DATA_BUS]   vram_mask_o,
    output  [`VRAM_DATA_BUS]   vram_din_o,
    input   [`VRAM_DATA_BUS]   vram_dout_i
);

// ========== 标准VLE64/VSE64的直通连接 ==========
assign vram_ren_o   = vmem_ren_i;
assign vram_wen_o   = vmem_wen_i;
assign vram_addr_o  = vmem_addr_i;

// ========== VLX加载扩展逻辑 ==========
// VLX: 从内存读取N个width位的数据，扩展到64位，存入向量寄存器
// width编码: [2:0] 基础宽度, 额外使用vs2字段的bit 0来表示符号/零扩展
// 通过vmem_din_i[0]传递: 0=零扩展(无符号), 1=符号扩展(有符号)
reg [`VMEM_DATA_BUS] vlx_result;
integer i;
wire is_signed_ext;
assign is_signed_ext = vmem_is_vlx_i ? vmem_din_i[0] : 1'b0;

always @(*) begin
    vlx_result = {`VLEN{1'b0}};  // 默认全0
    
    if (vmem_is_vlx_i) begin
        // len: 0-7 表示 1-8个元素
        // width: 000=8bit, 001=16bit, 010=32bit, 011=64bit
        case (vmem_width_i)
            3'b000: begin  // 8-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        // 符号扩展或零扩展
                        vlx_result[i*64 +: 64] = is_signed_ext ? 
                            {{56{vram_dout_i[i*8+7]}}, vram_dout_i[i*8 +: 8]} :
                            {{56{1'b0}}, vram_dout_i[i*8 +: 8]};
                    end
                end
            end
            3'b001: begin  // 16-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vlx_result[i*64 +: 64] = is_signed_ext ?
                            {{48{vram_dout_i[i*16+15]}}, vram_dout_i[i*16 +: 16]} :
                            {{48{1'b0}}, vram_dout_i[i*16 +: 16]};
                    end
                end
            end
            3'b010: begin  // 32-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vlx_result[i*64 +: 64] = is_signed_ext ?
                            {{32{vram_dout_i[i*32+31]}}, vram_dout_i[i*32 +: 32]} :
                            {{32{1'b0}}, vram_dout_i[i*32 +: 32]};
                    end
                end
            end
            3'b011: begin  // 64-bit (Copy)
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vlx_result[i*64 +: 64] = vram_dout_i[i*64 +: 64];
                    end
                end
            end
            default: vlx_result = {`VLEN{1'b0}};
        endcase
    end
end

// ========== VSX存储截断逻辑 ==========
// VSX: 从向量寄存器取N个64位元素，截断到width位，写入内存
reg [`VMEM_DATA_BUS] vsx_data;

always @(*) begin
    vsx_data = {`VLEN{1'b0}};
    
    if (vmem_is_vsx_i) begin
        case (vmem_width_i)
            3'b000: begin  // 8-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        // 截断64位到8位
                        vsx_data[i*8 +: 8] = vmem_din_i[i*64 +: 8];
                    end
                end
            end
            3'b001: begin  // 16-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vsx_data[i*16 +: 16] = vmem_din_i[i*64 +: 16];
                    end
                end
            end
            3'b010: begin  // 32-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vsx_data[i*32 +: 32] = vmem_din_i[i*64 +: 32];
                    end
                end
            end
            3'b011: begin  // 64-bit
                for (i = 0; i <= vmem_len_i; i = i + 1) begin
                    if (i < `VLMAX) begin
                        vsx_data[i*64 +: 64] = vmem_din_i[i*64 +: 64];
                    end
                end
            end
            default: vsx_data = {`VLEN{1'b0}};
        endcase
    end
end

// ========== 输出选择 ==========
// 生成正确的写入mask
reg [`VRAM_DATA_BUS] vsx_mask;
integer j;

always @(*) begin
    vsx_mask = {`VLEN{1'b0}};
    
    if (vmem_is_vsx_i) begin
        case (vmem_width_i)
            3'b000: begin  // 8-bit
                for (j = 0; j <= vmem_len_i; j = j + 1) begin
                    if (j < `VLMAX) begin
                        vsx_mask[j*8 +: 8] = {8{1'b1}};
                    end
                end
            end
            3'b001: begin  // 16-bit
                for (j = 0; j <= vmem_len_i; j = j + 1) begin
                    if (j < `VLMAX) begin
                        vsx_mask[j*16 +: 16] = {16{1'b1}};
                    end
                end
            end
            3'b010: begin  // 32-bit
                for (j = 0; j <= vmem_len_i; j = j + 1) begin
                    if (j < `VLMAX) begin
                        vsx_mask[j*32 +: 32] = {32{1'b1}};
                    end
                end
            end
            3'b011: begin  // 64-bit
                for (j = 0; j <= vmem_len_i; j = j + 1) begin
                    if (j < `VLMAX) begin
                        vsx_mask[j*64 +: 64] = {64{1'b1}};
                    end
                end
            end
            default: vsx_mask = {`VLEN{1'b0}};
        endcase
    end else begin
        vsx_mask = {`VLEN{1'b1}};  // 非VSX指令时全1
    end
end

assign vmem_dout_o = vmem_is_vlx_i ? vlx_result : vram_dout_i;
assign vram_din_o  = vmem_is_vsx_i ? vsx_data : vmem_din_i;
assign vram_mask_o = vsx_mask;

endmodule