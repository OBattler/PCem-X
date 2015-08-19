/* Code borrowed from DOSBox and adapted by OBattler. */

#include <stdlib.h>
#include <stdint.h>
#include <math.h>

#include "ibm.h"
#include "device.h"
#include "io.h"
#include "mem.h"
#include "timer.h"
#include "video.h"
#include "vid_cga.h"
#include "vid_cga_comp.h"

static double hue_offset = 0.0;
static bool new_cga = 0;
static bool is_bw = 0;
static bool is_bpp1 = 0;

static uint8_t comp_pal[256][3];

void configure_comp(double h, uint8_t n, uint8_t bw, uint8_t b1)
{
	hue_offset = h;
	new_cga = n;
	is_bw = bw;
	is_bpp1 = b1;
}

uint8_t get_color(uint8_t index, uint8_t cyc, uint8_t col)
{
	uint8_t ind = (index & 0x3F) | ((cyc & 1) == 0 ? 0x30 : 0x80) | ((cyc & 2) == 0 ? 0x40 : 0);
	return comp_pal[ind][col];
}

void update_cga16_color(cga_t *cga)
{
// New algorithm based on code by reenigne
// Works in all CGA graphics modes/color settings and can simulate older and newer CGA revisions
	uint8_t cga16_val = cga->cgacol;

	static const double tau = 6.28318531; // == 2*pi
	static const double ns = 567.0/440;  // degrees of hue shift per nanosecond

	double tv_brightness = 0.0; // hardcoded for simpler implementation
	double tv_saturation = (new_cga ? 0.7 : 0.6);

	// bool bw = (vga.tandy.mode_control&4) != 0;
	bool bw = is_bw;
	bool color_sel = (cga16_val&0x20) != 0;
	bool background_i = (cga16_val&0x10) != 0;	// Really foreground intensity, but this is what the CGA schematic calls it.
	// bool bpp1 = (vga.tandy.mode_control&0x10) != 0;
	bool bpp1 = is_bpp1;
	Bit8u overscan = cga16_val&0x0f;  // aka foreground colour in 1bpp mode

	double chroma_coefficient = new_cga ? 0.29 : 0.72;
	double b_coefficient = new_cga ? 0.07 : 0;
	double g_coefficient = new_cga ? 0.22 : 0;
	double r_coefficient = new_cga ? 0.1 : 0;
	double i_coefficient = new_cga ? 0.32 : 0.28;
	double rgbi_coefficients[0x10];

	int c = 0;
	double a, b2, c2, d, d2, x, v;
	double Y, I, Q, R, G, B;
	int r, g, b;
	double contrast, chroma, composite, hue_adjust;
	Bit8u cc, xx, bits, p, i, j, index;
	double chroma_signals[8][4];
	Bit8u rgbi;
	bool even;

	static const double gamma = 2.2;

	Bitu CGApal[4] = {
		overscan,
		2 + (color_sel||bw ? 1 : 0) + (background_i ? 8 : 0),
		4 + (color_sel&&!bw? 1 : 0) + (background_i ? 8 : 0),
		6 + (color_sel||bw ? 1 : 0) + (background_i ? 8 : 0)
	};

	static const double phases[6] = {
		270 - 21.5*ns,  // blue
		135 - 29.5*ns,  // green
		180 - 21.5*ns,  // cyan
		  0 - 21.5*ns,  // red
		315 - 29.5*ns,  // magenta
		 90 - 21.5*ns}; // yellow/burst

	for (cc = 0; c < 0x10; c++) {
		v = 0;
		if ((cc & 1) != 0)
			v += b_coefficient;
		if ((cc & 2) != 0)
			v += g_coefficient;
		if ((cc & 4) != 0)
			v += r_coefficient;
		if ((cc & 8) != 0)
			v += i_coefficient;
		rgbi_coefficients[cc] = v;
	}

	// The pixel clock delay calculation is not accurate for 2bpp, but the difference is small and a more accurate calculation would be too slow.
	static const double rgbi_pixel_delay = 15.5*ns;
	static const double chroma_pixel_delays[8] = {
		0,        // Black:   no chroma
		35*ns,    // Blue:    no XORs
		44.5*ns,  // Green:   XOR on rising and falling edges
		39.5*ns,  // Cyan:    XOR on falling but not rising edge
		44.5*ns,  // Red:     XOR on rising and falling edges
		39.5*ns,  // Magenta: XOR on falling but not rising edge
		44.5*ns,  // Yellow:  XOR on rising and falling edges
		39.5*ns}; // White:   XOR on falling but not rising edge
	double pixel_clock_delay;
	int o = overscan == 0 ? 15 : overscan;
	if (overscan == 8)
		pixel_clock_delay = rgbi_pixel_delay;
	else {
		d2 = rgbi_coefficients[o];
		pixel_clock_delay = (chroma_pixel_delays[o & 7]*chroma_coefficient + rgbi_pixel_delay*d2)/(chroma_coefficient + d2);
	}
	pixel_clock_delay -= 21.5*ns;  // correct for delay of color burst

	hue_adjust = (-(90-33)-hue_offset+pixel_clock_delay)*tau/360.0;
	for (i=0; i<4; i++) {
		chroma_signals[0][i] = 0;
		chroma_signals[7][i] = 1;
		for (j=0; j<6; j++) {
			// All the duty cycle fractions are the same, just under 0.5 as the rising edge is delayed 2ns more than the falling edge.
			static const double duty = 0.5 - 2*ns/360.0;

			// We have a rectangle wave with period 1 (in units of the reciprocal of the color burst frequency) and duty
			// cycle fraction "duty" and phase "phase". We band-limit this wave to frequency 2 and sample it at intervals of 1/4.
			// We model our band-limited wave with 4 frequency components:
			//   f(x) = a + b*sin(x*tau) + c*cos(x*tau) + d*sin(x*2*tau)
			// Then:
			//   a =   integral(0, 1, f(x)*dx) = duty
			//   b = 2*integral(0, 1, f(x)*sin(x*tau)*dx) = 2*integral(0, duty, sin(x*tau)*dx) = 2*(1-cos(x*tau))/tau
			//   c = 2*integral(0, 1, f(x)*cos(x*tau)*dx) = 2*integral(0, duty, cos(x*tau)*dx) = 2*sin(duty*tau)/tau
			//   d = 2*integral(0, 1, f(x)*sin(x*2*tau)*dx) = 2*integral(0, duty, sin(x*4*pi)*dx) = 2*(1-cos(2*tau*duty))/(2*tau)
			a = duty;
			b2 = 2.0*(1.0-cos(duty*tau))/tau;
			c2 = 2.0*sin(duty*tau)/tau;
			d = 2.0*(1.0-cos(duty*2*tau))/(2*tau);

			x = (phases[j] + 21.5*ns + pixel_clock_delay)/360.0 + i/4.0;

			chroma_signals[j+1][i] = a + b2*sin(x*tau) + c2*cos(x*tau) + d*sin(x*2*tau);
		}
	}
	for (xx=0; xx<4; xx++) {	 // Position of pixel in question
		even = (xx & 1) == 0;
		for (bits=0; bits<(even ? 0x10 : 0x40); ++bits) {
			Y=0, I=0, Q=0;
			for (p=0; p<4; p++) {  // Position within color carrier cycle
				// generate pixel pattern.
				if (bpp1)
					rgbi = ((bits >> (3-p)) & (even ? 1 : 2)) != 0 ? overscan : 0;
				else
					if (even)
						rgbi = CGApal[(bits >> (2-(p&2)))&3];
					else
						rgbi = CGApal[(bits >> (4-((p+1)&6)))&3];
				cc = rgbi & 7;
				if (bw && cc != 0)
					cc = 7;

				// calculate composite output
				chroma = chroma_signals[cc][(p+xx)&3]*chroma_coefficient;
				composite = chroma + rgbi_coefficients[rgbi];

				Y+=composite;
				if (!bw) { // burst on
					I+=composite*2*cos(hue_adjust + (p+xx)*tau/4.0);
					Q+=composite*2*sin(hue_adjust + (p+xx)*tau/4.0);
				}
			}

			contrast = 1 - tv_brightness;

			Y = (contrast*Y/4.0) + tv_brightness; if (Y>1.0) Y=1.0; if (Y<0.0) Y=0.0;
			I = (contrast*I/4.0) * tv_saturation; if (I>0.5957) I=0.5957; if (I<-0.5957) I=-0.5957;
			Q = (contrast*Q/4.0) * tv_saturation; if (Q>0.5226) Q=0.5226; if (Q<-0.5226) Q=-0.5226;

			R = Y + 0.9563*I + 0.6210*Q;	R = (R - 0.075) / (1-0.075); if (R<0) R=0; if (R>1) R=1;
			G = Y - 0.2721*I - 0.6474*Q;	G = (G - 0.075) / (1-0.075); if (G<0) G=0; if (G>1) G=1;
			B = Y - 1.1069*I + 1.7046*Q;	B = (B - 0.075) / (1-0.075); if (B<0) B=0; if (B>1) B=1;
			R = pow(R, gamma);
			G = pow(G, gamma);
			B = pow(B, gamma);

			r = (int)(255*pow( 1.5073*R -0.3725*G -0.0832*B, 1/gamma)); if (r<0) r=0; if (r>255) r=255;
			g = (int)(255*pow(-0.0275*R +0.9350*G +0.0670*B, 1/gamma)); if (g<0) g=0; if (g>255) g=255;
			b = (int)(255*pow(-0.0272*R -0.0401*G +1.1677*B, 1/gamma)); if (b<0) b=0; if (b>255) b=255;

			index = bits | ((xx & 1) == 0 ? 0x30 : 0x80) | ((xx & 2) == 0 ? 0x40 : 0);
			// RENDER_SetPal(index,r,g,b);
			comp_pal[index][0] = r;
			comp_pal[index][1] = g;
			comp_pal[index][2] = b;
			pclog("Index is: %i (cgacol %i)\n", index, cga->cgacol);
		}
	}

	// pclog("Composite mode: from %i to (%i, %i, %i)\n", cga16_val, *rr, *gg, *bb);
}

void IncreaseHue(bool pressed) {
	if (!pressed)
		return;
	hue_offset += 5.0;
	// update_cga16_color();
	pclog("Hue at %f", hue_offset); 
}

void DecreaseHue(bool pressed) {
	if (!pressed)
		return;
	hue_offset -= 5.0;
	// update_cga16_color();
	pclog("Hue at %f", hue_offset); 
}

