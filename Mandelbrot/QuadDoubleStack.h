#pragma once

class QuadDoubleStack
{
private:
	typedef struct Node
	{
		struct Node* _next;
		double       _dxMin;
		double       _dxMax;
		double       _dyMin;
		double       _dyMax;
	} Node;
	struct Node* _top;
public:
	QuadDoubleStack();
	~QuadDoubleStack();
	void push(HWND hWnd, double dxMin, double dxMax, double dyMin, double dyMax);
	BOOL pop(HWND hWnd, double& dxMin, double& dxMax, double& dyMin, double& dyMax);
};
