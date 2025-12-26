`timescale 1ns / 1ps

// 改进的脉动阵列，支持与向量加速器集成
// 特性：
// 1. 通过向量指令控制所有数据传输
// 2. 支持随机访问输出（非脉动输出）
// 3. Output Stationary: 支持partial sum累加
// 4. 适合分块矩阵乘法

module systolic_array #(
  parameter ARRAY_SIZE_M_MAX = 8,
  parameter ARRAY_SIZE_K_MAX = 8,
  parameter DATA_WIDTH = 64
) (
  input                                    clk,
  input                                    rst_n,
  
  // ========== Configuration Interface ==========
  input                                   config_valid,
  input [8:0]                             cfg_M,
  input [8:0]                             cfg_N, 
  input [8:0]                             cfg_K,
  
  // ========== X Buffer Write Interface ==========
  // 向量指令通过此接口写入X数据
  input                                   x_wr_en,
  input [8:0]                             x_wr_idx,  // N维度索引 (0 to N-1)
  input [DATA_WIDTH*ARRAY_SIZE_M_MAX-1:0] x_wr_data,
  
  // ========== W Buffer Write Interface ==========
  // 向量指令通过此接口写入W数据
  input                                   w_wr_en,
  input [8:0]                             w_wr_idx,  // N维度索引 (0 to N-1)
  input [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] w_wr_data,
  
  // ========== Control Interface ==========
  input                                    start,      // 启动计算
  input                                    accumulate, // 1: 累加模式, 0: 清零后计算
  
  // ========== Output Read Interface ==========
  // 向量指令通过此接口读取结果
  input                                    y_rd_en,
  input [8:0]                              y_rd_row,   // 读取哪一行 (0 to M-1)
  output [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] y_rd_data,
  output                                   y_rd_valid,
  
  // ========== Status ==========
  output reg                               busy,
  output reg                               done
);

  // 参数
  localparam MAX_N = 8;  // 最大N维度（可调整）
  
  // 配置寄存器
  reg [8:0] M, N, K;
  
  // X和W缓冲区
  reg [DATA_WIDTH*ARRAY_SIZE_M_MAX-1:0] X_buffer [0:MAX_N-1];
  reg [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] W_buffer [0:MAX_N-1];
  
  // PE阵列连接
  wire [DATA_WIDTH-1:0] x_bus [ARRAY_SIZE_M_MAX-1:0][ARRAY_SIZE_K_MAX:0];
  wire [DATA_WIDTH-1:0] w_bus [ARRAY_SIZE_M_MAX:0][ARRAY_SIZE_K_MAX-1:0];
  wire [DATA_WIDTH-1:0] y_bus [ARRAY_SIZE_M_MAX-1:0][ARRAY_SIZE_K_MAX-1:0];
  
  // 脉动延迟缓冲区
  reg [DATA_WIDTH-1:0] x_delay_buf [ARRAY_SIZE_M_MAX-1:0][ARRAY_SIZE_M_MAX-2:0];
  reg [DATA_WIDTH-1:0] w_delay_buf [ARRAY_SIZE_K_MAX-2:0][ARRAY_SIZE_K_MAX-1:0];
  
  // 当前输入
  reg [DATA_WIDTH*ARRAY_SIZE_M_MAX-1:0] X_current;
  reg [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] W_current;
  
  // PE控制信号
  wire output_en;
  reg accumulate_en;
  
  genvar i, j, k;
  
  // ========== Configuration Register ==========
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      M <= 0;
      N <= 0;
      K <= 0;
    end else if (config_valid) begin
      M <= cfg_M;
      N <= cfg_N;
      K <= cfg_K;
    end
  end
  
  // ========== X Buffer ==========
  always @(posedge clk) begin
    if (x_wr_en && x_wr_idx < MAX_N) begin
      X_buffer[x_wr_idx] <= x_wr_data;
    end
  end
  
  // ========== W Buffer ==========
  always @(posedge clk) begin
    if (w_wr_en && w_wr_idx < MAX_N) begin
      W_buffer[w_wr_idx] <= w_wr_data;
    end
  end
  
  // ========== State Machine ==========
  localparam STATE_IDLE    = 2'b00;
  localparam STATE_COMPUTE = 2'b01;
  localparam STATE_DONE    = 2'b10;
  
  reg [1:0] state;
  reg [31:0] cycle_count;
  reg [8:0] n_count;  // 当前处理的N索引
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      state <= STATE_IDLE;
      cycle_count <= 0;
      n_count <= 0;
      busy <= 0;
      done <= 0;
      accumulate_en <= 0;
      X_current <= 0;
      W_current <= 0;
    end else begin
      case (state)
        STATE_IDLE: begin
          busy <= 0;
          done <= 0;
          cycle_count <= 0;
          n_count <= 0;
          
          if (start && M != 0 && N != 0 && K != 0) begin
            state <= STATE_COMPUTE;
            busy <= 1;
            accumulate_en <= accumulate;
            n_count <= 0;
            cycle_count <= 0;
          end
        end
        
        STATE_COMPUTE: begin
          busy <= 1;
          cycle_count <= cycle_count + 1;
          
          // 喂入数据
          if (n_count < N) begin
            X_current <= X_buffer[n_count];
            W_current <= W_buffer[n_count];
            n_count <= n_count + 1;
          end else begin
            // N个数据已全部喂入，等待流水线flush
            X_current <= 0;
            W_current <= 0;
          end
          
          // 计算完成条件：N个cycle喂数据 + (M+K-2)个cycle flush
          if (cycle_count >= N + M + K - 2) begin
            state <= STATE_DONE;
            busy <= 0;
            done <= 1;
          end
        end
        
        STATE_DONE: begin
          done <= 1;
          busy <= 0;
          // 保持done信号，直到下次start
          if (start) begin
            state <= STATE_IDLE;
          end
        end
        
        default: state <= STATE_IDLE;
      endcase
    end
  end
  
  // ========== PE Array Instantiation ==========
  generate
    for (i = 0; i < ARRAY_SIZE_M_MAX; i = i + 1) begin : gen_pe_row
      for (j = 0; j < ARRAY_SIZE_K_MAX; j = j + 1) begin : gen_pe_col
        pe #(
          .DATA_WIDTH(DATA_WIDTH)
        ) pe_inst (
          .clk          (clk),
          .rst_n        (rst_n),
          .clear        (!accumulate_en && (state == STATE_IDLE || (state == STATE_COMPUTE && cycle_count == 0))),
          .bypass       ((i >= M || j >= K) ? 1'b1 : 1'b0),
          .y_in         ((i > 0) ? y_bus[i-1][j] : {DATA_WIDTH{1'b0}}),
          .x_in         (x_bus[i][j]),
          .w_in         (w_bus[i][j]),
          .x_out        (x_bus[i][j+1]),
          .w_out        (w_bus[i+1][j]),
          .y_out        (y_bus[i][j])
        );
      end
    end
  endgenerate
  
  // ========== X Dataflow with Skew ==========
  generate
    for (i = 0; i < ARRAY_SIZE_M_MAX; i = i + 1) begin : gen_x_skew
      if (i == 0) begin
        assign x_bus[i][0] = X_current[DATA_WIDTH*(i+1)-1:DATA_WIDTH*i];
      end else begin
        assign x_bus[i][0] = x_delay_buf[i][0];
        
        for (k = 0; k < i; k = k + 1) begin : gen_x_delay
          always @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
              x_delay_buf[i][k] <= 0;
            end else begin
              if (k == i - 1) begin
                x_delay_buf[i][k] <= X_current[DATA_WIDTH*(i+1)-1:DATA_WIDTH*i];
              end else begin
                x_delay_buf[i][k] <= x_delay_buf[i][k + 1];
              end
            end
          end
        end
      end
    end
  endgenerate
  
  // ========== W Dataflow with Skew ==========
  generate
    for (j = 0; j < ARRAY_SIZE_K_MAX; j = j + 1) begin : gen_w_skew
      if (j == 0) begin
        assign w_bus[0][j] = W_current[DATA_WIDTH*(j+1)-1:DATA_WIDTH*j];
      end else begin
        assign w_bus[0][j] = w_delay_buf[0][j];
        
        for (k = 0; k < j; k = k + 1) begin : gen_w_delay
          always @(posedge clk or negedge rst_n) begin
            if (!rst_n) begin
              w_delay_buf[k][j] <= 0;
            end else begin
              if (k == j - 1) begin
                w_delay_buf[k][j] <= W_current[DATA_WIDTH*(j+1)-1:DATA_WIDTH*j];
              end else begin
                w_delay_buf[k][j] <= w_delay_buf[k + 1][j];
              end
            end
          end
        end
      end
    end
  endgenerate
  
  // ========== Output Read Interface ==========
  // 随机访问输出，读取指定行的结果
  reg [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] y_rd_data_reg;
  reg y_rd_valid_reg;
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      y_rd_data_reg <= 0;
      y_rd_valid_reg <= 0;
    end else begin
      if (y_rd_en && y_rd_row < M) begin
        // 从指定行读取所有K个结果
        for (j = 0; j < ARRAY_SIZE_K_MAX; j = j + 1) begin
          y_rd_data_reg[DATA_WIDTH*(j+1)-1:DATA_WIDTH*j] <= y_bus[y_rd_row][j];
        end
        y_rd_valid_reg <= 1;
      end else begin
        y_rd_valid_reg <= 0;
      end
    end
  end
  
  assign y_rd_data = y_rd_data_reg;
  assign y_rd_valid = y_rd_valid_reg;
  
endmodule

// ========== 改进的PE单元 ==========
// 支持清零和累加控制
module pe #(
  parameter DATA_WIDTH = 64
) (
  input                      clk,
  input                      rst_n,
  input                      clear,    // 清零partial sum
  input                      bypass,   // 旁路此PE
  input  [DATA_WIDTH-1:0]   y_in,     // 来自上方的partial sum
  input  [DATA_WIDTH-1:0]   x_in,     // 来自左侧的数据
  input  [DATA_WIDTH-1:0]   w_in,     // 来自上方的权重
  output [DATA_WIDTH-1:0]   x_out,    // 向右传递
  output [DATA_WIDTH-1:0]   w_out,    // 向下传递
  output reg [DATA_WIDTH-1:0] y_out   // 向下传递partial sum
);

  reg [DATA_WIDTH-1:0] x_reg, w_reg;
  wire [DATA_WIDTH-1:0] mac_result;
  
  // MAC: y_out = y_in + x * w (如果不bypass)
  assign mac_result = bypass ? y_in : (y_in + x_reg * w_reg);
  
  always @(posedge clk or negedge rst_n) begin
    if (!rst_n) begin
      x_reg <= 0;
      w_reg <= 0;
      y_out <= 0;
    end else begin
      if (clear) begin
        y_out <= 0;
      end else begin
        y_out <= mac_result;
      end
      
      x_reg <= x_in;
      w_reg <= w_in;
    end
  end
  
  assign x_out = x_reg;
  assign w_out = w_reg;
  
endmodule
