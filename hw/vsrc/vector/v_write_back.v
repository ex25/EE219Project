//写回寄存器（可能来自MEM或ALU）

`include "v_defines.v"

module v_write_back (
    input                      vid_wb_en_i,
    input                      vid_wb_sel_i,
    input                      vid_wb_split_i,
    input   [`VREG_ADDR_BUS]   vid_wb_addr_i,
    input   [`VREG_BUS]        valu_result_i,
    input   [`VREG_BUS]        vmem_result_i,

    output                     vwb_en_o,
    output  [`VREG_ADDR_BUS]   vwb_addr_o,
    output  [`VREG_BUS]        vwb_data_o
);

    // ----------------------------
    // 写使能与写地址
    // ----------------------------
    assign vwb_en_o   = vid_wb_en_i;
    assign vwb_addr_o = vid_wb_addr_i;

    // ----------------------------
    // 写数据选择：
    //   vid_wb_sel_i=1 -> 来自内存(vle32)
    //   vid_wb_sel_i=0 -> 来自ALU(vadd/vmul)
    //
    // ----------------------------
    wire [`VREG_BUS] mem_data_final;//这里是因为可能会需要对mem进来的数据做切分处理，所以多存一步
    assign mem_data_final = vmem_result_i; 

    assign vwb_data_o = (vid_wb_sel_i) ? mem_data_final : valu_result_i;

endmodule
