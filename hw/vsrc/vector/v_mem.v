//负责和VRAM交互进行读写。也就是说从指定的内存地址，例如0x8100_0000读
//直接简单地直通连接。


module v_mem (
   
    input                      vmem_ren_i,
    input                      vmem_wen_i,
    input   [`VMEM_ADDR_BUS]   vmem_addr_i,
    input   [`VMEM_DATA_BUS]   vmem_din_i,
    output  [`VMEM_DATA_BUS]   vmem_dout_o,

    output                     vram_ren_o,
    output                     vram_wen_o,
    output  [`VRAM_ADDR_BUS]   vram_addr_o,
    output  [`VRAM_DATA_BUS]   vram_mask_o,
    output  [`VRAM_DATA_BUS]   vram_din_o,
    input   [`VRAM_DATA_BUS]   vram_dout_i
);

assign vram_ren_o   = vmem_ren_i ;
assign vram_wen_o   = vmem_wen_i ;
assign vram_addr_o  = vmem_addr_i ;//指令保证不会同时从mem读出又写入，故此使用统一的addr。
assign vram_din_o   = vmem_din_i ;
assign vram_mask_o  = {(`VLEN){1'b1}};
assign vmem_dout_o  = vram_dout_i ;

endmodule