#pragma once

struct sRGB {
	double r;  // a fraction between 0 and 1
	double g;  // a fraction between 0 and 1
	double b;  // a fraction between 0 and 1
};

struct sHSV {
	double h;  // angle in degrees
	double s;  // a fraction between 0 and 1
	double v;  // a fraction between 0 and 1
};

sHSV mandelbrotHSV(int i, int max_iterations);
sRGB hsv2rgb(sHSV in);
uint32_t ReverseRGBBytes(uint32_t input);
