#pragma once

class WorkQueue
{
private:
	typedef struct WorkItem
	{
		struct WorkItem* _next;
		struct WorkItem* _last;
		int              _StartPixel;
		int              _EndPixel;
	} WorkItem;
	WorkItem* _head;
	WorkItem* _tail;
	int       _Slices;
public:
	WorkQueue();
	~WorkQueue();
	void Enqueue(int StartPixel, int EndPixel);
	BOOL Dequeue(int& StartPixel, int& EndPixel);
	int getSlices() { return _Slices; }
};