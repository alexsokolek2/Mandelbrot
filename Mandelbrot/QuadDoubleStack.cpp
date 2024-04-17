// QuadDoubleStack.cpp - Class implementing a LIFO stack.

#include "framework.h"

#include "QuadDoubleStack.h"

// Create empty recall stack
QuadDoubleStack::QuadDoubleStack()
{
	_top = NULL;
}

// Delete recall stack
QuadDoubleStack::~QuadDoubleStack()
{
	while (_top != NULL)
	{
		Node* temp = _top;
		_top = _top->_next;
		delete temp;
	}
}

// Push old coordinates onto the recall stack
void QuadDoubleStack::push(HWND hWnd, double dxMin, double dxMax, double dyMin, double dyMax)
{
	Node* temp = new Node;
	if (temp == NULL)
	{
		MessageBeep(MB_ICONEXCLAMATION);
		MessageBox(hWnd, _T("FATAL: Unable to allocate Node!"),
		                 _T("QuadDoubleStack::push"), MB_OK | MB_ICONEXCLAMATION);
		ExitProcess(2);
	}
	
	temp->_dxMin = dxMin;
	temp->_dxMax = dxMax;
	temp->_dyMin = dyMin;
	temp->_dyMax = dyMax;

	temp->_next = _top;
	_top = temp;
}

// Pop old coordinate off of the recall stack.
BOOL QuadDoubleStack::pop(HWND hWnd, double& dxMin, double& dxMax, double& dyMin, double& dyMax)
{
	if (_top == NULL)
	{
		MessageBeep(MB_ICONEXCLAMATION);
		MessageBox(hWnd, _T("Recall stack is empty!"),
		                 _T("QuadDoubleStack::pop"), MB_OK | MB_ICONEXCLAMATION);
		return false;
	}

	dxMin = _top->_dxMin;
	dxMax = _top->_dxMax;
	dyMin = _top->_dyMin;
	dyMax = _top->_dyMax;

	Node* temp = _top->_next;
	delete _top;
	_top = temp;

	return true;
}
