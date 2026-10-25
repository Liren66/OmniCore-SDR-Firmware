
`timescale 1 ns / 1 ps

	module CH32H417_IP_v1_0 #
	(
		// Users to add parameters here

		// User parameters ends
		// Do not modify the parameters beyond this line


		// Parameters of Axi Slave Bus Interface S00_AXI
		parameter integer C_S00_AXI_DATA_WIDTH	= 32,
		parameter integer C_S00_AXI_ADDR_WIDTH	= 4
	)
	(
		// Users to add ports here
        //CH32H417 PORT
        output wire			Interrupt_flag,
        input  wire         CH32H417_CLK,
        input  wire         CH32H417_CS_n,
        input  wire         CH32H417_WR_n,
        input  wire         CH32H417_RD_n,
        input  wire         CH32H417_OE_n,
        input  wire         CH32H417_EOP_n,
        input  wire [1:0]   CH32H417_SEL,
        inout  wire [31:0]  CH32H417_DATA,
        output wire         CH32H417_RDNE,
        output wire         CH32H417_WRNF,
        output wire         CH32H417_AF_n,
        output wire         CH32H417_AE_n,
        
        //ADC Interface
        input  wire         adc_clk,
		input  wire [15:0]  adc_data_i0,
		input  wire [15:0]  adc_data_q0,
		input  wire [15:0]  adc_data_i1,
		input  wire [15:0]  adc_data_q1,
		input  wire         adc_valid_i0,
		input  wire         adc_valid_q0,
		input  wire         adc_valid_i1,
		input  wire         adc_valid_q1,
		input  wire         adc_enable_i0,
		input  wire         adc_enable_q0,
		input  wire         adc_enable_i1,
		input  wire         adc_enable_q1,
		
		//DAC interface
        input  wire         dac_clk,
        input  wire [15:0]  dac_data_i0,
        input  wire [15:0]  dac_data_q0,
        input  wire [15:0]  dac_data_i1,
        input  wire [15:0]  dac_data_q1,
        input  wire         dac_valid_i0,
        input  wire         dac_valid_q0,
        input  wire         dac_valid_i1,
        input  wire         dac_valid_q1,
        input  wire         dac_enable_i0,
        input  wire         dac_enable_q0,
        input  wire         dac_enable_i1,
        input  wire         dac_enable_q1,
    
        output wire [15:0]  dac_data_i0_out,
        output wire [15:0]  dac_data_q0_out,
        output wire [15:0]  dac_data_i1_out,
        output wire [15:0]  dac_data_q1_out,
		// User ports ends
		// Do not modify the ports beyond this line


		// Ports of Axi Slave Bus Interface S00_AXI
		input wire  s00_axi_aclk,
		input wire  s00_axi_aresetn,
		input wire [C_S00_AXI_ADDR_WIDTH-1 : 0] s00_axi_awaddr,
		input wire [2 : 0] s00_axi_awprot,
		input wire  s00_axi_awvalid,
		output wire  s00_axi_awready,
		input wire [C_S00_AXI_DATA_WIDTH-1 : 0] s00_axi_wdata,
		input wire [(C_S00_AXI_DATA_WIDTH/8)-1 : 0] s00_axi_wstrb,
		input wire  s00_axi_wvalid,
		output wire  s00_axi_wready,
		output wire [1 : 0] s00_axi_bresp,
		output wire  s00_axi_bvalid,
		input wire  s00_axi_bready,
		input wire [C_S00_AXI_ADDR_WIDTH-1 : 0] s00_axi_araddr,
		input wire [2 : 0] s00_axi_arprot,
		input wire  s00_axi_arvalid,
		output wire  s00_axi_arready,
		output wire [C_S00_AXI_DATA_WIDTH-1 : 0] s00_axi_rdata,
		output wire [1 : 0] s00_axi_rresp,
		output wire  s00_axi_rvalid,
		input  wire  s00_axi_rready
	);
	wire adc_valid_0;
	wire adc_valid_1;
	wire adc_enable_0;
	wire adc_enable_1;
	
	wire dac_valid_0;
	wire dac_valid_1;
	wire dac_enable_0;
	wire dac_enable_1;
	
	assign CH32H417_AE_n = 1'b0;
	assign CH32H417_AF_n = 1'b0;
	assign adc_valid_0 = adc_valid_i0 & adc_valid_q0;
	assign adc_valid_1 = adc_valid_i1 & adc_valid_q1;
	assign adc_enable_0 = adc_enable_i0 & adc_enable_q0;
	assign adc_enable_1 = adc_enable_i1 & adc_enable_q1;
	
	assign dac_valid_0 = dac_valid_i0 & dac_valid_q0;
	assign dac_valid_1 = dac_valid_i1 & dac_valid_q1;
	assign dac_enable_0 = dac_enable_i0 & dac_enable_q0;
	assign dac_enable_1 = dac_enable_i1 & dac_enable_q1;
	
// Instantiation of Axi Bus Interface S00_AXI
	CH32H417_IP_v1_0_S00_AXI # ( 
		.C_S_AXI_DATA_WIDTH(C_S00_AXI_DATA_WIDTH),
		.C_S_AXI_ADDR_WIDTH(C_S00_AXI_ADDR_WIDTH)
	) CH32H417_IP_v1_0_S00_AXI_inst (
		//User ports
		.Interrupt_flag(Interrupt_flag),
		.CH32H417_CLK(CH32H417_CLK),
		.CH32H417_CS_n(CH32H417_CS_n),
		.CH32H417_WR_n(CH32H417_WR_n),
		.CH32H417_RD_n(CH32H417_RD_n),
		.CH32H417_OE_n(CH32H417_OE_n),
		.CH32H417_EOP_n(CH32H417_EOP_n),
		.CH32H417_SEL(CH32H417_SEL),
		.CH32H417_DATA(CH32H417_DATA),
		.CH32H417_RDNE(CH32H417_RDNE),
		.CH32H417_WRNF(CH32H417_WRNF),

		//ADC Interface
		.adc_clk(adc_clk),
		.adc_data_i0(adc_data_i0),
		.adc_data_q0(adc_data_q0),
		.adc_data_i1(adc_data_i1),
		.adc_data_q1(adc_data_q1),
		.adc_valid_0(adc_valid_0),
		.adc_valid_1(adc_valid_1),
		.adc_enable_0(adc_enable_0),
		.adc_enable_1(adc_enable_1),

		//DAC Interface
		.dac_clk(dac_clk),
		.dac_data_i0(dac_data_i0),
		.dac_data_q0(dac_data_q0),
		.dac_data_i1(dac_data_i1),
		.dac_data_q1(dac_data_q1),
		.dac_valid_0(dac_valid_0),
		.dac_valid_1(dac_valid_1),
		.dac_enable_0(dac_enable_0),
		.dac_enable_1(dac_enable_1),
		.dac_data_i0_out(dac_data_i0_out),
		.dac_data_q0_out(dac_data_q0_out),
		.dac_data_i1_out(dac_data_i1_out),
		.dac_data_q1_out(dac_data_q1_out),

		//AXI ports
		.S_AXI_ACLK(s00_axi_aclk),
		.S_AXI_ARESETN(s00_axi_aresetn),
		.S_AXI_AWADDR(s00_axi_awaddr),
		.S_AXI_AWPROT(s00_axi_awprot),
		.S_AXI_AWVALID(s00_axi_awvalid),
		.S_AXI_AWREADY(s00_axi_awready),
		.S_AXI_WDATA(s00_axi_wdata),
		.S_AXI_WSTRB(s00_axi_wstrb),
		.S_AXI_WVALID(s00_axi_wvalid),
		.S_AXI_WREADY(s00_axi_wready),
		.S_AXI_BRESP(s00_axi_bresp),
		.S_AXI_BVALID(s00_axi_bvalid),
		.S_AXI_BREADY(s00_axi_bready),
		.S_AXI_ARADDR(s00_axi_araddr),
		.S_AXI_ARPROT(s00_axi_arprot),
		.S_AXI_ARVALID(s00_axi_arvalid),
		.S_AXI_ARREADY(s00_axi_arready),
		.S_AXI_RDATA(s00_axi_rdata),
		.S_AXI_RRESP(s00_axi_rresp),
		.S_AXI_RVALID(s00_axi_rvalid),
		.S_AXI_RREADY(s00_axi_rready)
	);

	// Add user logic here
    
	// User logic ends

	endmodule
