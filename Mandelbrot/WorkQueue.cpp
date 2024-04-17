// WorkQueue.cpp - Class of work items for the thread worker pool.
// 
// A doubly linked list, each node containing a StartPixel and an EndPixel.
 
#include "framework.h"
#include "WorkQueue.h"

WorkQueue::WorkQueue()
{
	_head = NULL;
	_tail = NULL;
}

WorkQueue::~WorkQueue()
{
	while (_head != NULL)
	{
		WorkItem* temp = _head->_next;
		delete _head;
		_head = temp;
		_head->_last = NULL;
	}
}

void WorkQueue::Enqueue(int StartPixel, int EndPixel)
{
	// Allocate new work item.
	WorkItem* temp = new WorkItem;
	temp->_StartPixel = StartPixel;
	temp->_EndPixel = EndPixel;

	// Insert the first work item.
	if (_head == NULL)
	{
		temp->_next = NULL;
		temp->_last = NULL;
		_head = temp;
		_tail = temp;
		return;
	}

	// Insert the second and subsequent work item.
	_head->_last = temp;
	temp->_next = _head;
	temp->_last = NULL;
	_head = temp;
	return;
}

BOOL WorkQueue::Dequeue(int& StartPixel, int& EndPixel)
{
	// Check for no more work items.
	if (_tail == NULL) return false;
	
	// Get the last work item.
	StartPixel = _tail->_StartPixel;
	EndPixel = _tail->_EndPixel;
	
	// Delete the only work item.
	if (_head == _tail)
	{
		delete _head;
		_head = NULL;
		_tail = NULL;
		return true;
	}

	// Delete the last work item.
	WorkItem* temp = _tail;
	_tail->_last->_next = NULL;
	_tail = _tail->_last;
	delete temp;
	return true;
}
