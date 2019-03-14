# DB_BufferManager

This project, written in __c++__, shows the internal of a data processing engine. I built a buffer manager, on
top of an I/O Layer that was provided.

### Buffer Manager concepts/background:
A __database buffer pool__ is an array of fixed-sized memory buffers called frames that are used to hold database pages (also called disk blocks) that have been read from disk into memory. 

A __page__ is the unit of transfer between the disk and the buffer pool residing in main memory. Most modern database systems use a page size of at least 8,192 bytes.

Another important thing to note is that a database page in memory is an exact copy of the corresponding page on disk when it is first read in. Once a page has been read from disk to the buffer pool, the DBMS software can update information stored on the page, causing the copy in the buffer pool to be different from the copy on disk. Such pages are termed __dirty__.

Since the database on disk itself is often larger than the amount of main memory that is available for the buffer pool, __only a subset of the database pages__ fit in memory at any given time. The buffer manager is used to control which pages are memory resident. Whenever the buffer manager receives a request for a data page, the buffer manager checks to see if the requested page is already in the one of the frames that constitutes the buffer pool. If so, the buffer manager simply returns a pointer to the page. If not, the buffer manager frees a frame (possibly by writing to disk the page it contains if the page is dirty) and then reads in the requested page from disk into the frame that has been freed.

### Buffer Replacement Policies and the Clock Algorithm
There are many ways of deciding which page to replace when a free frame is needed. Commonly used policies in operating systems are __FIFO, MRU and LRU__. Even though LRU is one of the most commonly used policies it has high overhead and is not the best strategy to use in a number of common cases that occur in database systems. Instead, many systems use the clock algorithm that approximates LRU behavior and is much faster.

the buffer pool contains numBufs frames, numbered 0 to numBufs-1. Conceptually, all the frames in the buffer pool are arranged in a circular list. Associated with each frame is a bit termed the refbit. Each time a page in the buffer pool is accessed (via a readPage() call to the buffer manager) the refbit of the corresponding frame is set to true. At any point in time the clock hand (an integer whose value is between 0 and numBufs - 1) is advanced (using modular arithmetic so that it does not go past numBufs - 1) in a clockwise fashion.

For each frame that the clockhand goes past, the refbit is examined and then cleared. If the bit had been set, the corresponding frame has been referenced "recently" and is not replaced. On the other hand, if the refbit is false, the page is selected for replacement (assuming it is not pinned - pinned pages are discussed below). If the selected buffer frame is dirty (ie. it has been modified), the page currently occupying the frame is written back to disk. Otherwise the frame is just cleared and a new page from disk is read in to that location.

### The Structure of the Buffer Manager.
The BadgerDB buffer manager uses three C++ classes: _**BufMgr, BufDesc and BufHashTbl**_. There is only one instance of the __BufMgr__ class. A key component of this class is the actual buffer pool which consists of an array of numBufs frames, each the size of a database page. In addition to this array, the BufMgr instance also contains an array of numBufs instances of the __BufDesc__ class that is used to describe the state of each frame in the buffer pool. A hash table is used to keep track of the pages that are currently resident in the buffer pool. This hash table is implemented by an instance of the __BufHashTbl__ class. This instance is a private data member of the BufMgr class.

