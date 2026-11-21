`timescale 1ns / 1ps
`default_nettype none
/*
 doom_draw.v

 Ф1в (core-hexen): the drawer of the view: the plain columns and the spans of the renderer (src/fw/hexen/
 r_draw.c, KG_HWDRAW) drawn into the work frame of the SRAM while the CPU goes on. Same pixels as the
 C code (the ISS models it: src/sim/iss.cpp Drawer).

 MMIO (doom_soc, offsets of 0x80000000, src/fw/mmio.h):
   C0 W  DRW0: offset in the frame (15..0) | pixels - 1 (24..16) | colormap index (30..25)
   C4 W  FRAC: the column frac (16.16), or the span position (x 6.10 high, y 6.10 low)
   C8 W  STEP: its step
   CC W  COL: the SDRAM byte offset of the column source: queues a column
   D0 W  SPAN: the SDRAM byte offset of the 64x64 flat: queues a span
   D4 W  CMAP: a word of the colormaps (the address auto-increments by 4)
   D8 W  CMADR: the byte address of the next CMAP word
   DC R  STAT: { 8'hD7, 23'd0, busy };  W: forget the cached flat
 A write of COL / SPAN waits (no ack: push_wait) while the queue is full.

 Column: pixel i = cmap[cm][src[(frac >> 16) & 127]] at off + 320 i, frac += step. The source bytes come
 from a line buffer of 4 lines of 32 bytes (direct mapped by address bits 6..5, cleared per command),
 filled by SDRAM line reads (8 words, CTI 010 as the caches of VexRiscv).
 Span: pixel i = cmap[cm][flat[spot]] at off + i, spot = ((pos >> 4) & 0xfc0) | (pos >> 26), pos += step.
 The flat (4 KB) is kept in a block RAM with its address; a new address loads it (128 line reads).
 Colormaps: 32 tables of 256 bytes (8 KB; Hexen has 34: the last two stay with the CPU).
 Output: an open halfword (two pixels of a span merge into one SRAM write) and a write buffer handed to
 doom_sram (drw_* port, a write of the lanes of one halfword). busy: anything queued or not written.
*/
module doom_draw (
	input wire clk,
	input wire reset,

	// MMIO
	input wire io_we,
	input wire [7:0] io_off,
	input wire [31:0] io_wd,
	output wire push_wait,               // a COL / SPAN write must wait (the queue is full)
	output wire busy,

	// SDRAM master (line reads)
	output reg sd_cyc = 1'b0,
	output reg [24:2] sd_adr = 23'd0,
	output wire [2:0] sd_cti,
	input wire sd_ack,
	input wire [31:0] sd_dat,

	// SRAM write port (doom_sram), halfword addresses
	output wire sr_req,
	output wire [20:0] sr_adr,
	output wire [1:0] sr_sel,
	output wire [15:0] sr_dat,
	input wire sr_ack
);

localparam [20:0] FBH = 21'h1f8000;      // the work frame 0x3f0000 in halfwords

// ------------------------------------------------------------------ the engine state

localparam [2:0] E_IDLE = 3'd0, E_PIX = 3'd1, E_FT = 3'd2, E_CM = 3'd3, E_FILL = 3'd4, E_FLAT = 3'd5;
reg [2:0] st = E_IDLE;
reg c_span = 1'b0;
reg [15:0] c_off = 16'd0;
reg [8:0] c_n = 9'd0;                    // pixels left - 1
reg [4:0] c_cm = 5'd0;
reg [31:0] c_frac = 32'd0, c_step = 32'd0;
reg [24:0] c_src = 25'd0;
reg [1:0] c_byte = 2'd0;                 // the byte of cm_q of this pixel
reg [7:0] c_tex = 8'd0;
reg [2:0] f_beat = 3'd0;                 // line read: the word acked next
reg [7:0] f_line = 8'd0;                 // flat load: the line
reg [7:0] f_lines = 8'd0;                // its lines - 1 (128, or 129 for a flat not on a line)
reg f_last = 1'b0;

// ------------------------------------------------------------------ the queue

reg [30:0] r_w0 = 31'd0;
reg [31:0] r_frac = 32'd0, r_step = 32'd0;

// one entry: { span, w0[30:0], frac, step, src[24:0] } = 121 bits; written once per clock, no reset
reg [120:0] q_mem [0:15];
reg [3:0] q_wp = 4'd0, q_rp = 4'd0;
reg [4:0] q_n = 5'd0;
wire q_full = q_n == 5'd16;
wire q_empty = q_n == 5'd0;
wire push = io_we && (io_off == 8'hcc || io_off == 8'hd0) && !q_full;
assign push_wait = q_full;
wire pop;

always @(posedge clk)
	if (push) q_mem[q_wp] <= { io_off == 8'hd0, r_w0, r_frac, r_step, io_wd[24:0] };

always @(posedge clk) begin
	if (reset) begin
		q_wp <= 4'd0; q_rp <= 4'd0; q_n <= 5'd0;
	end else begin
		if (push) q_wp <= q_wp + 4'd1;
		if (pop) q_rp <= q_rp + 4'd1;
		case ({ push, pop })
		2'b10: q_n <= q_n + 5'd1;
		2'b01: q_n <= q_n - 5'd1;
		default: ;
		endcase
	end
	if (io_we) case (io_off)
		8'hc0: r_w0 <= io_wd[30:0];
		8'hc4: r_frac <= io_wd;
		8'hc8: r_step <= io_wd;
		default: ;
	endcase
end
wire [120:0] q_head = q_mem[q_rp];

// ------------------------------------------------------------------ the colormaps (2048 x 32)

reg [31:0] cm_mem [0:2047];
reg [12:2] cm_wa = 11'd0;
reg [31:0] cm_q = 32'd0;
wire [12:0] cm_ra;                       // byte address { colormap[4:0], texel }
always @(posedge clk) begin
	if (io_we && io_off == 8'hd4) cm_mem[cm_wa] <= io_wd;
	cm_q <= cm_mem[cm_ra[12:2]];
end
always @(posedge clk)
	if (io_we && io_off == 8'hd8) cm_wa <= io_wd[12:2];
	else if (io_we && io_off == 8'hd4) cm_wa <= cm_wa + 11'd1;

// ------------------------------------------------------------------ the flat (1024 x 32)

reg [31:0] fl_mem [0:1023];
reg [31:0] fl_q = 32'd0;
wire [9:0] fl_ra;
reg fl_valid = 1'b0;
reg [24:0] fl_src = 25'd0;
// a word of the flat load: its index from the flat start (the lines are aligned, the flat only to 4)
wire [22:0] fl_rel = sd_adr - fl_src[24:2];
wire fl_we = st == E_FLAT && sd_cyc && sd_ack && fl_rel < 23'd1024;
always @(posedge clk) begin
	if (fl_we) fl_mem[fl_rel[9:0]] <= sd_dat;
	fl_q <= fl_mem[fl_ra];
end

// ------------------------------------------------------------------ the line buffer of a column (32 x 32)

reg [31:0] lb_mem [0:31];
// written in the clock of the ack: the next E_PIX reads it
wire lb_we = st == E_FILL && sd_ack;
always @(posedge clk)
	if (lb_we) lb_mem[{ sd_adr[6:5], f_beat }] <= sd_dat;
reg [3:0] lb_v = 4'd0;
reg [19:0] lb_tag0 = 20'd0, lb_tag1 = 20'd0, lb_tag2 = 20'd0, lb_tag3 = 20'd0;

// ------------------------------------------------------------------ the engine


assign pop = st == E_IDLE && !q_empty;
assign sd_cti = (f_beat == 3'd7) ? 3'b111 : 3'b010;

// the column source byte of this pixel
wire [24:0] c_a = c_src + { 18'd0, c_frac[22:16] };
wire [1:0] c_slot = c_a[6:5];
wire [19:0] c_tag = c_slot == 2'd0 ? lb_tag0 : c_slot == 2'd1 ? lb_tag1 : c_slot == 2'd2 ? lb_tag2 : lb_tag3;
wire c_hit = lb_v[c_slot] && c_tag == c_a[24:5];
wire [31:0] c_word = lb_mem[{ c_slot, c_a[4:2] }];
wire [7:0] c_texel = c_a[1:0] == 2'd0 ? c_word[7:0] : c_a[1:0] == 2'd1 ? c_word[15:8] :
                     c_a[1:0] == 2'd2 ? c_word[23:16] : c_word[31:24];
// the span spot of this pixel
wire [11:0] s_spot = { c_frac[15:10], c_frac[31:26] };
assign fl_ra = s_spot[11:2];
reg [1:0] s_byte = 2'd0;
wire [7:0] s_texel = s_byte == 2'd0 ? fl_q[7:0] : s_byte == 2'd1 ? fl_q[15:8] : s_byte == 2'd2 ? fl_q[23:16] : fl_q[31:24];
// the colormap read: in E_PIX for a column (its texel now), in E_FT for a span
wire [7:0] tex_now = c_span ? s_texel : c_texel;
assign cm_ra = { c_cm, tex_now };
wire [7:0] pix = c_byte == 2'd0 ? cm_q[7:0] : c_byte == 2'd1 ? cm_q[15:8] : c_byte == 2'd2 ? cm_q[23:16] : cm_q[31:24];

// ------------------------------------------------------------------ the output

reg ob_v = 1'b0;                         // the open halfword
reg [20:0] ob_adr = 21'd0;
reg [1:0] ob_sel = 2'b00;
reg [15:0] ob_dat = 16'd0;
reg wb_v = 1'b0;                         // the write handed to doom_sram
reg [20:0] wb_adr = 21'd0;
reg [1:0] wb_sel = 2'b00;
reg [15:0] wb_dat = 16'd0;
assign sr_req = wb_v;
assign sr_adr = wb_adr;
assign sr_sel = wb_sel;
assign sr_dat = wb_dat;

wire [20:0] px_adr = FBH + { 6'd0, c_off[15:1] };
wire wb_free = !wb_v || sr_ack;
// E_CM puts the pixel: into the open halfword (same halfword), or the open one goes to the write buffer
wire px_same = ob_v && ob_adr == px_adr;
wire px_ok = px_same || !ob_v || wb_free;
wire px_put = st == E_CM && px_ok;
// the open halfword is written when full, or when the engine has nothing more for it
wire ob_flush = ob_v && !px_put && (ob_sel == 2'b11 || st == E_IDLE || st == E_FILL || st == E_FLAT) && wb_free;

assign busy = !q_empty || st != E_IDLE || ob_v || wb_v;

always @(posedge clk) begin
	if (sr_ack) wb_v <= 1'b0;

	if (px_put) begin
		if (px_same) begin
			if (c_off[0]) ob_dat[15:8] <= pix; else ob_dat[7:0] <= pix;
			ob_sel <= ob_sel | (c_off[0] ? 2'b10 : 2'b01);
		end else begin
			if (ob_v) begin
				wb_v <= 1'b1; wb_adr <= ob_adr; wb_sel <= ob_sel; wb_dat <= ob_dat;
			end
			ob_v <= 1'b1;
			ob_adr <= px_adr;
			ob_sel <= c_off[0] ? 2'b10 : 2'b01;
			ob_dat <= c_off[0] ? { pix, 8'd0 } : { 8'd0, pix };
		end
	end else if (ob_flush) begin
		wb_v <= 1'b1; wb_adr <= ob_adr; wb_sel <= ob_sel; wb_dat <= ob_dat;
		ob_v <= 1'b0;
	end

	if (io_we && io_off == 8'hdc) fl_valid <= 1'b0;

	if (reset) begin
		st <= E_IDLE;
		sd_cyc <= 1'b0;
		ob_v <= 1'b0;
		wb_v <= 1'b0;
		fl_valid <= 1'b0;
	end else case (st)
	E_IDLE: if (!q_empty) begin
		c_span <= q_head[120];
		c_off <= q_head[104:89];
		c_n <= q_head[113:105];
		c_cm <= q_head[118:114];
		c_frac <= q_head[88:57];
		c_step <= q_head[56:25];
		c_src <= q_head[24:0];
		lb_v <= 4'd0;
		if (q_head[120] && !(fl_valid && fl_src == q_head[24:0])) begin
			fl_valid <= 1'b0;
			fl_src <= q_head[24:0];
			f_line <= 8'd0;
			f_lines <= (q_head[4:2] != 3'd0) ? 8'd128 : 8'd127;
			f_beat <= 3'd0;
			sd_adr <= { q_head[24:5], 3'd0 };
			sd_cyc <= 1'b1;
			st <= E_FLAT;
		end else
			st <= E_PIX;
	end

	// a column: the texel from the line buffer (a miss reads its line), the colormap read starts
	// a span: the flat read starts (spot of c_frac)
	E_PIX: if (c_span) begin
		s_byte <= s_spot[1:0];
		st <= E_FT;
	end else if (c_hit) begin
		c_byte <= c_texel[1:0];
		st <= E_CM;
	end else begin
		lb_v[c_slot] <= 1'b0;
		f_beat <= 3'd0;
		sd_adr <= { c_a[24:5], 3'd0 };
		sd_cyc <= 1'b1;
		st <= E_FILL;
	end
	// a span: the flat word is out, the colormap read starts
	E_FT: begin
		c_byte <= s_texel[1:0];
		st <= E_CM;
	end
	// the pixel is out of the colormap: put it, next
	E_CM: if (px_ok) begin
		c_off <= c_off + (c_span ? 16'd1 : 16'd320);
		c_frac <= c_frac + c_step;
		c_n <= c_n - 9'd1;
		st <= (c_n == 9'd0) ? E_IDLE : E_PIX;
	end

	// a line of the column source (8 words) into the slot of its address
	E_FILL: if (sd_ack) begin
		sd_adr <= sd_adr + 23'd1;
		f_beat <= f_beat + 3'd1;
		if (f_beat == 3'd7) begin
			sd_cyc <= 1'b0;
			lb_v[sd_adr[6:5]] <= 1'b1;
			case (sd_adr[6:5])
			2'd0: lb_tag0 <= sd_adr[24:5];
			2'd1: lb_tag1 <= sd_adr[24:5];
			2'd2: lb_tag2 <= sd_adr[24:5];
			default: lb_tag3 <= sd_adr[24:5];
			endcase
			st <= E_PIX;
		end
	end

	// the flat: 128 lines (129 when it does not start a line); the owner of the SDRAM is given up after each (sd_cyc low a clock)
	E_FLAT: if (sd_cyc && sd_ack) begin
		sd_adr <= sd_adr + 23'd1;
		f_beat <= f_beat + 3'd1;
		if (f_beat == 3'd7) begin
			sd_cyc <= 1'b0;
			f_line <= f_line + 8'd1;
			if (f_line == f_lines) begin
				fl_valid <= 1'b1;
				st <= E_PIX;
			end
		end
	end else if (!sd_cyc)
		sd_cyc <= 1'b1;
	default: st <= E_IDLE;
	endcase
end

endmodule
