`timescale 1ns / 1ps
// One clock domain. One outstanding AXI-Lite write and read, independently.
module board_ctrl_axi #(
    parameter integer CLK_FREQ_HZ = 100000000
) (
    (* X_INTERFACE_INFO = "xilinx.com:signal:clock:1.0 S_AXI_CLK CLK", X_INTERFACE_PARAMETER = "ASSOCIATED_BUSIF S_AXI, ASSOCIATED_RESET s_axi_aresetn" *)
    input  wire        s_axi_aclk,
    (* X_INTERFACE_INFO = "xilinx.com:signal:reset:1.0 S_AXI_RST RST", X_INTERFACE_PARAMETER = "POLARITY ACTIVE_LOW" *)
    input  wire        s_axi_aresetn,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWADDR", X_INTERFACE_PARAMETER = "PROTOCOL AXI4LITE, DATA_WIDTH 32, ADDR_WIDTH 12, READ_WRITE_MODE READ_WRITE, HAS_BURST 0, HAS_LOCK 0, HAS_CACHE 0, HAS_QOS 0, HAS_REGION 0, SUPPORTS_NARROW_BURST 0, NUM_READ_OUTSTANDING 1, NUM_WRITE_OUTSTANDING 1" *)
    input  wire [11:0] s_axi_awaddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWPROT" *)
    input  wire [2:0]  s_axi_awprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWVALID" *)
    input  wire        s_axi_awvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI AWREADY" *)
    output wire        s_axi_awready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WDATA" *)
    input  wire [31:0] s_axi_wdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WSTRB" *)
    input  wire [3:0]  s_axi_wstrb,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WVALID" *)
    input  wire        s_axi_wvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI WREADY" *)
    output wire        s_axi_wready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BRESP" *)
    output reg  [1:0]  s_axi_bresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BVALID" *)
    output reg         s_axi_bvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI BREADY" *)
    input  wire        s_axi_bready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARADDR" *)
    input  wire [11:0] s_axi_araddr,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARPROT" *)
    input  wire [2:0]  s_axi_arprot,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARVALID" *)
    input  wire        s_axi_arvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI ARREADY" *)
    output wire        s_axi_arready,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RDATA" *)
    output reg  [31:0] s_axi_rdata,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RRESP" *)
    output reg  [1:0]  s_axi_rresp,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RVALID" *)
    output reg         s_axi_rvalid,
    (* X_INTERFACE_INFO = "xilinx.com:interface:aximm:1.0 S_AXI RREADY" *)
    input  wire        s_axi_rready,
    output wire        rgb_data,
    output wire        fan_pwm
);
    localparam [31:0] DEFAULT_PERIOD = CLK_FREQ_HZ / 25000;
    reg [23:0] rgb;
    reg [31:0] fan_period, fan_high;
    reg [11:0] write_addr;
    reg [31:0] write_data;
    reg [3:0]  write_strb;
    reg addr_pending, data_pending;
    integer lane;

    // AW and W are captured separately: either channel may arrive first.
    assign s_axi_awready = s_axi_aresetn && !addr_pending && !s_axi_bvalid;
    assign s_axi_wready  = s_axi_aresetn && !data_pending && !s_axi_bvalid;
    assign s_axi_arready = s_axi_aresetn && !s_axi_rvalid;

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            rgb <= 24'd0;
            fan_period <= DEFAULT_PERIOD;
            fan_high <= 32'd0;
            write_addr <= 12'd0;
            write_data <= 32'd0;
            write_strb <= 4'd0;
            addr_pending <= 1'b0;
            data_pending <= 1'b0;
            s_axi_bvalid <= 1'b0;
            s_axi_bresp <= 2'b00;
        end else begin
            if (s_axi_awready && s_axi_awvalid) begin
                write_addr <= s_axi_awaddr;
                addr_pending <= 1'b1;
            end
            if (s_axi_wready && s_axi_wvalid) begin
                write_data <= s_axi_wdata;
                write_strb <= s_axi_wstrb;
                data_pending <= 1'b1;
            end
            if (s_axi_bvalid && s_axi_bready)
                s_axi_bvalid <= 1'b0;
            if (addr_pending && data_pending && !s_axi_bvalid) begin
                addr_pending <= 1'b0;
                data_pending <= 1'b0;
                s_axi_bvalid <= 1'b1;
                s_axi_bresp <= 2'b00;
                case (write_addr)
                    12'h000: for (lane = 0; lane < 3; lane = lane + 1)
                        if (write_strb[lane])
                            rgb[lane*8 +: 8] <= write_data[lane*8 +: 8];
                    12'h004: for (lane = 0; lane < 4; lane = lane + 1)
                        if (write_strb[lane])
                            fan_period[lane*8 +: 8] <= write_data[lane*8 +: 8];
                    12'h008: for (lane = 0; lane < 4; lane = lane + 1)
                        if (write_strb[lane])
                            fan_high[lane*8 +: 8] <= write_data[lane*8 +: 8];
                    default: s_axi_bresp <= 2'b10; // SLVERR: RO/unmapped/unaligned
                endcase
            end
        end
    end

    always @(posedge s_axi_aclk) begin
        if (!s_axi_aresetn) begin
            s_axi_rvalid <= 1'b0;
            s_axi_rdata <= 32'd0;
            s_axi_rresp <= 2'b00;
        end else begin
            if (s_axi_rvalid && s_axi_rready)
                s_axi_rvalid <= 1'b0;
            if (s_axi_arready && s_axi_arvalid) begin
                s_axi_rvalid <= 1'b1;
                s_axi_rresp <= 2'b00;
                case (s_axi_araddr)
                    12'h000: s_axi_rdata <= {8'd0, rgb};
                    12'h004: s_axi_rdata <= fan_period;
                    12'h008: s_axi_rdata <= fan_high;
                    12'h00c: s_axi_rdata <= CLK_FREQ_HZ;
                    default: begin
                        s_axi_rdata <= 32'd0;
                        s_axi_rresp <= 2'b10;
                    end
                endcase
            end
        end
    end

    board_ctrl_ws2812 #(.CLK_FREQ_HZ(CLK_FREQ_HZ)) led (
        .clk(s_axi_aclk), .resetn(s_axi_aresetn), .rgb(rgb), .data_out(rgb_data)
    );
    board_ctrl_pwm #(.DEFAULT_PERIOD(DEFAULT_PERIOD)) fan (
        .clk(s_axi_aclk), .resetn(s_axi_aresetn),
        .period_request(fan_period), .high_request(fan_high), .pwm_out(fan_pwm)
    );
endmodule
