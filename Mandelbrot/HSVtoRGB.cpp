#include "framework.h"
#include "HSVtoRGB.h"

sHSV mandelbrotHSV(int iteration, int max_iterations) {
	sHSV color;
	static double lllmax_iterations = log(log(log(max_iterations)));
	if (iteration == max_iterations) // Case of in the set - black
	{
		color.h = 0.0;
		color.s = 0.0;
		color.v = 0.0;
	}
	else
	{
		// Hue is 0 to 360 based on iteration to max_iterations.
		// Saturation and Value are always max.
		double norm_iteration = log(log(log((double)iteration))) / lllmax_iterations;
		color.h = norm_iteration * 360.0;
		color.s = 1.0;
		color.v = 1.0;
	}
	return color;
}

sRGB hsv2rgb(sHSV in) {
	double hh, p, q, t, ff;
	long i;
	sRGB out;

	if (in.s <= 0.0) {       // < is bogus, just shuts up warnings (???)
		out.r = in.v;
		out.g = in.v;
		out.b = in.v;
		return out;
	}

	hh = in.h;
	if (hh >= 360.0) hh = 0.0;
	hh /= 60.0;
	i = (long)hh;
	ff = hh - i;
	p = in.v * (1.0 - in.s);
	q = in.v * (1.0 - (in.s * ff));
	t = in.v * (1.0 - (in.s * (1.0 - ff)));

	switch (i) {
	case 0:
		out.r = in.v; out.g = t; out.b = p;
		break;
	case 1:
		out.r = q; out.g = in.v; out.b = p;
		break;
	case 2:
		out.r = p; out.g = in.v; out.b = t;
		break;
	case 3:
		out.r = p; out.g = q; out.b = in.v;
		break;
	case 4:
		out.r = t; out.g = p; out.b = in.v;
		break;
	case 5:
	default:
		out.r = in.v; out.g = p; out.b = q;
		break;
	}
	return out;
}

// ReverseRGBBytes is used to convert little-endian
// format to big-endian format for the dc, or the
// reverse, I don't know which, which is what the
// GPU needs. RGB becomes BGR. Alpha (A) is ignored
// and left set to zero. Only used in the RGB system.

uint32_t ReverseRGBBytes(uint32_t input)
{
	uint32_t temp = 0;
	temp += (input & 0x000000ff) << 16;
	temp += (input & 0x0000ff00);
	temp += (input & 0x00ff0000) >> 16;
	return temp;
}
