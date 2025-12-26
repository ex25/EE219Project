# 脉动阵列与向量加速器集成设计文档

## 1. 设计概述

### 1.1 核心改进
- **完全解耦内存访问**：通过向量指令控制所有数据传输，脉动阵列不直接访问内存
- **灵活的输出访问**：支持随机读取任意行，而非脉动输出
- **Output Stationary数据流**：PE保留partial sum，支持多次累加
- **简化的控制接口**：清晰的状态和握手信号

### 1.2 关键优势
1. **分块矩阵乘法友好**：通过累加模式，可以处理任意大小矩阵
2. **向量处理器协同**：完全通过向量指令控制，无需额外总线
3. **高效利用**：Output Stationary减少数据搬移
4. **可扩展性**：参数化设计，易于调整阵列大小

## 2. 硬件接口设计

### 2.1 配置接口
```verilog
input        config_valid,
input [8:0]  cfg_M,        // 矩阵A的行数
input [8:0]  cfg_N,        // 共享维度（K在外部循环）
input [8:0]  cfg_K,        // 矩阵B的列数
```

**使用方式**：
- 向量指令`vsa_config M, N, K`触发
- 硬件解码指令，提取M/N/K并置位config_valid

### 2.2 数据写入接口

#### X Buffer（矩阵A的数据）
```verilog
input                                   x_wr_en,
input [8:0]                            x_wr_idx,   // 0 to N-1
input [DATA_WIDTH*ARRAY_SIZE_M_MAX-1:0] x_wr_data,
```

**使用方式**：
- 软件先将一列A数据加载到向量寄存器
- 执行`vsa_load_x vs, idx`
- 硬件将vs的内容写入X_buffer[idx]

#### W Buffer（矩阵B的数据）
```verilog
input                                   w_wr_en,
input [8:0]                            w_wr_idx,   // 0 to N-1
input [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] w_wr_data,
```

**使用方式**：类似X Buffer

### 2.3 控制接口
```verilog
input  start,        // 启动计算
input  accumulate,   // 累加模式控制
output busy,         // 正在计算
output done,         // 计算完成
```

**状态机**：
```
IDLE -> COMPUTE -> DONE -> IDLE
  ^                   |
  |___________________|
```

### 2.4 输出读取接口
```verilog
input                                    y_rd_en,
input [8:0]                             y_rd_row,   // 读取哪一行
output [DATA_WIDTH*ARRAY_SIZE_K_MAX-1:0] y_rd_data,
output                                   y_rd_valid,
```

**使用方式**：
- 执行`vsa_read_y vd, row`
- 硬件从y_bus[row][0..K-1]读取数据
- 将结果写入向量寄存器vd

## 3. 向量指令集扩展

### 3.1 指令格式

| 指令 | 格式 | 编码 |
|------|------|------|
| vsa_config | M, N, K | [31:23]=M, [22:14]=N, [13:5]=K |
| vsa_load_x | vs, idx | [31:23]=idx, [22:20]=vs |
| vsa_load_w | vs, idx | [31:23]=idx, [22:20]=vs |
| vsa_start | accumulate | [20]=accum |
| vsa_read_y | vd, row | [31:23]=row, [11:7]=vd |
| vsa_status | vd | [11:7]=vd |

### 3.2 指令解码逻辑（伪代码）

```verilog
// 在v_inst_decode.v中添加
case (opcode)
  OPCODE_VSA_CONFIG: begin
    sa_config_valid <= 1'b1;
    sa_cfg_M <= inst[31:23];
    sa_cfg_N <= inst[22:14];
    sa_cfg_K <= inst[13:5];
  end
  
  OPCODE_VSA_LOAD_X: begin
    sa_x_wr_en <= 1'b1;
    sa_x_wr_idx <= inst[31:23];
    sa_x_wr_data <= vreg[inst[22:20]];  // 从向量寄存器读取
  end
  
  OPCODE_VSA_LOAD_W: begin
    sa_w_wr_en <= 1'b1;
    sa_w_wr_idx <= inst[31:23];
    sa_w_wr_data <= vreg[inst[22:20]];
  end
  
  OPCODE_VSA_START: begin
    sa_start <= 1'b1;
    sa_accumulate <= inst[20];
  end
  
  OPCODE_VSA_READ_Y: begin
    sa_y_rd_en <= 1'b1;
    sa_y_rd_row <= inst[31:23];
    vreg[inst[11:7]] <= sa_y_rd_data;  // 写入向量寄存器
  end
  
  OPCODE_VSA_STATUS: begin
    vreg[inst[11:7]][0] <= sa_busy;
    vreg[inst[11:7]][1] <= sa_done;
  end
endcase
```

## 4. 集成到向量处理器

### 4.1 模块连接（在v_rvcpu.v中）

```verilog
// 实例化脉动阵列
systolic_array_v2 #(
  .ARRAY_SIZE_M_MAX(8),
  .ARRAY_SIZE_K_MAX(8),
  .DATA_WIDTH(64)
) sa_inst (
  .clk          (clk),
  .rst_n        (rst_n),
  
  // 配置接口
  .config_valid (sa_config_valid),
  .cfg_M        (sa_cfg_M),
  .cfg_N        (sa_cfg_N),
  .cfg_K        (sa_cfg_K),
  
  // X写接口
  .x_wr_en      (sa_x_wr_en),
  .x_wr_idx     (sa_x_wr_idx),
  .x_wr_data    (sa_x_wr_data),
  
  // W写接口
  .w_wr_en      (sa_w_wr_en),
  .w_wr_idx     (sa_w_wr_idx),
  .w_wr_data    (sa_w_wr_data),
  
  // 控制接口
  .start        (sa_start),
  .accumulate   (sa_accumulate),
  
  // 输出接口
  .y_rd_en      (sa_y_rd_en),
  .y_rd_row     (sa_y_rd_row),
  .y_rd_data    (sa_y_rd_data),
  .y_rd_valid   (sa_y_rd_valid),
  
  // 状态
  .busy         (sa_busy),
  .done         (sa_done)
);
```

### 4.2 信号定义

在向量处理器中添加连接信号：
```verilog
// 脉动阵列控制信号
wire        sa_config_valid;
wire [8:0]  sa_cfg_M, sa_cfg_N, sa_cfg_K;
wire        sa_x_wr_en, sa_w_wr_en;
wire [8:0]  sa_x_wr_idx, sa_w_wr_idx;
wire [511:0] sa_x_wr_data, sa_w_wr_data;  // 8*64bit
wire        sa_start, sa_accumulate;
wire        sa_y_rd_en;
wire [8:0]  sa_y_rd_row;
wire [511:0] sa_y_rd_data;
wire        sa_y_rd_valid;
wire        sa_busy, sa_done;
```

## 5. 软件使用流程

### 5.1 基本流程
```c
// 1. 配置维度
vsa_config(M, N, K);

// 2. 加载数据（循环N次）
for (int i = 0; i < N; i++) {
    // 准备X数据（A的第i列）
    // 准备W数据（B的第i行）
    vlx(v1, ...);  // 加载X
    vlx(v2, ...);  // 加载W
    vsa_load_x(v1, i);
    vsa_load_w(v2, i);
}

// 3. 启动计算
vsa_start(0);  // 清零模式

// 4. 等待完成
vsa_wait_done();

// 5. 读取结果
for (int i = 0; i < M; i++) {
    vsa_read_y(v3, i);
    vsx(v3, ...);  // 存储结果
}
```

### 5.2 分块矩阵乘法
参见 `systolic_example.c` 中的 `matmul_systolic_int64`

## 6. 性能优化建议

### 6.1 数据预加载
- 在等待计算时，可以预加载下一个tile的数据
- 使用软件流水线：加载tile(i+1) 的同时计算tile(i)

### 6.2 累加模式利用
- K维度分块时使用累加模式，减少partial sum搬移
- 适合大K但M、N适中的场景

### 6.3 Cache友好
- 按照行优先顺序访问A矩阵
- 按照列优先顺序访问B矩阵（已转置）

## 7. 测试建议

### 7.1 单元测试
1. 配置接口测试
2. 数据加载测试
3. 简单矩阵乘法（3x3）
4. 累加模式测试

### 7.2 集成测试
1. 与向量指令协同
2. 分块矩阵乘法（大矩阵）
3. 性能benchmark

## 8. 后续扩展方向

### 8.1 硬件优化
- 增加ARRAY_SIZE参数支持更大阵列
- 添加数据类型支持（int8, int16, fp16）
- 多阵列并行

### 8.2 软件优化
- 自动分块策略
- 循环展开优化
- 与其他向量指令混合调度

### 8.3 算法扩展
- 卷积加速
- 批量矩阵乘法
- Transformer attention计算
