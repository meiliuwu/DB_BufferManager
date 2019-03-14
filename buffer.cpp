/**
 * This file builds a buffer manager with necessary methods
 * 
 * @authors
 * 
 * Yifan Mei  9075633736  
 * Peng Cheng  9074016453  
 * Meiliu Wu 9077761311 
 * Chenlai Shi  9071530506 
 * 
 */

/**
 * @author See Contributors.txt for code contributors and overview of BadgerDB.
 *
 * @section LICENSE
 * Copyright (c) 2012 Database Group, Computer Sciences Department, University of Wisconsin-Madison.
 */

#include <memory>
#include <iostream>
#include "buffer.h"
#include "exceptions/buffer_exceeded_exception.h"
#include "exceptions/page_not_pinned_exception.h"
#include "exceptions/page_pinned_exception.h"
#include "exceptions/bad_buffer_exception.h"
#include "exceptions/hash_not_found_exception.h"

namespace badgerdb { 

BufMgr::BufMgr(std::uint32_t bufs)
  : numBufs(bufs) {
  bufDescTable = new BufDesc[bufs];

  for (FrameId i = 0; i < bufs; i++) 
  {
    bufDescTable[i].frameNo = i;
    bufDescTable[i].valid = false;
  }

  bufPool = new Page[bufs];

  int htsize = ((((int) (bufs * 1.2))*2)/2)+1;
  hashTable = new BufHashTbl (htsize);  // allocate the buffer hash table

  clockHand = bufs - 1;
}

/*This function is to free the total Buffer Manager. 
 @param nothing
 @return null
 inside this function, we deete the bufPool, bufDescTable, hashTable
 */
BufMgr::~BufMgr() { 
    // Flushes out all dirty pages 
    for (uint32_t i = 0; i < numBufs; i++){
        if (bufDescTable[i].dirty && bufDescTable[i].valid){
            bufDescTable[i].file -> writePage(bufPool[bufDescTable[i].frameNo]);
        }
    }
    
    //  deallocates the buffer pool and the BufDesc table.
    delete [] bufPool;
    delete [] bufDescTable;
    //free the hashTable by calling the ~BufHashTbl function
    hashTable->~BufHashTbl();
}

/*this function is to assign clockhand move on to the next.
* To prevent clockhand of oversize, we use remainder of numbufs.
* Inside this function, we change the value of clockHand
* @param nothing
* @return nothing
*/
void BufMgr::advanceClock()
{
    clockHand += 1;
    //to prevent clockhand of oversize, we use remainder of numbufs
    clockHand = clockHand % numBufs;
}

/*this function is to allocate a free frame, and use frameid to point to this new frame.
* inside this function, we change the frameid.
* If there is no free frame, throw bufferexceedexception
* @param the frame needed to allocate
* @return nothing
*/
void BufMgr::allocBuf(FrameId & frame)
{
  //check whether there is a free frame found or not
    bool flag = false;
    // in the worst case, need do 2 full rotations - 1 .
    // The worst case happens when the only free frame is refit = 1 now
    // and it is just ahead of our start clockhand
    uint32_t worst_num_rota = numBufs*2-1;
    //use for loop to count how many times clockhand walks
    for (uint32_t numframvisited = 0; numframvisited < worst_num_rota; numframvisited++){
        //if the frame is not valid, it is a free frame, we just find it.
        if (bufDescTable[clockHand].valid == false){
            frame = bufDescTable[clockHand].frameNo;
            flag = true;
            break;
        }
        //if clockhand just released, can't use it now. just set it to be zero.
        else if (bufDescTable[clockHand].refbit == true){
            bufDescTable[clockHand].refbit = false;
            advanceClock();
        }
        //if the frame is in use now
        else if (bufDescTable[clockHand].pinCnt > 0){
            advanceClock();
        }
        //previously been used but free to use now. p=0, ref = 0. choose this frame
        else{
          //check if dirty or not. If dirty, write to disk
            if (bufDescTable[clockHand].dirty == true){
                bufDescTable[clockHand].file -> writePage(bufPool[bufDescTable[clockHand].frameNo]);
            }
            //get the frame id, and free the hashtable
            frame = bufDescTable[clockHand].frameNo;
            flag = true;
            hashTable->remove(bufDescTable[clockHand].file, bufDescTable[clockHand].pageNo);
            bufDescTable[clockHand].Clear();
            break;
       }
    }
    //if no free frame at all, throw exception
    if (flag == false){
        throw BufferExceededException();
    }
 }


/* This functino is to readpage from disk to buffer.
* If the page is not on frame, use allocbuf to find free frame and link the frame id to the page no and file pointer
* If the page has already in the frame, just add pincount number and set refbit to be true.
* @param file pointer that want to read from
* @param page id that want to read from
* @param page pointer that want to read from
* @return nothing
 
 */
void BufMgr::readPage(File* file, const PageId pageNo, Page*& page)
{
  // set the frameid to be looked up later 
  FrameId fid;
  try{
    //check whether the page is in the buffer,if it is, do nothing but let pinCnt++ and refbit = true
    hashTable->lookup(file, pageNo, fid);
    bufDescTable[fid].refbit = true;
    bufDescTable[fid].pinCnt++;
  }
  // Page is not in the buffer pool. Then we need to put it into buffer. To do this
  //be aware that when we set a bufDesc, the pinCnt is default to be 1, and refbit is default to be true, so we do not need to set pinCnt to be 1 and refbit to be true again. However, this situation is only true at the first time. When we reuse pinCnt, its original becomes 0. So, to make sure refbit is true and pinCnt is 1, we still set them.
  catch(HashNotFoundException h){
      // use allocBuf to arrange a new frame id for the new page.
      allocBuf(fid);
      // use readPage to read page from disk to buffer manager
      bufPool[fid] = file->readPage(pageNo);
      // Put the file, the page id and the frame id into the hashtable. Insert a new entry.
      hashTable->insert(file, pageNo, fid);
      //put the file and page id linked to the frameid in the bufDescTable.
      bufDescTable[fid].Set(file, pageNo);
  }
  
  // Return a pointer to the frame containing the page via the page parameter.
  page = &bufPool[fid];
}


/*
* Decrements the pinCnt of the frame containing (file, PageNo)
* and, if dirty == true, sets the dirty bit.
* Throws PAGENOTPINNED if the pin count is already 0.
* Does nothing if page is not found in the hash table lookup.
* @param the corresponding file on disk, 
* @param the corresponding page number
* @param a boolean variable to show whether the frame is dirty or not
* @return nothing
*/

void BufMgr::unPinPage(File* file, const PageId pageNo, const bool dirty) 
{
  FrameId fid;
  //use lookup in hashTable to find corresponding fid that store given file+pageno
  try{
    // might throw HashNotFoundException
    hashTable->lookup(file, pageNo, fid);
    
    //if pinCnt is less than or equal to 0,, throw new exception.
    //Do we need to catch this type of exception?
    if (bufDescTable[fid].pinCnt <= 0) {
      throw PageNotPinnedException(file->filename(), pageNo, fid);
    } else {
      bufDescTable[fid].pinCnt--;
      //if dirty is true, set dirtybit to true
      if (dirty == true) {
        bufDescTable[fid].dirty = true;
      }
    }
    //decrement pinCnt
  }
  //if not find, do nothing
  catch (HashNotFoundException e2) {
  }
}


/*
* Should scan bufdescTable for pages belonging to the file.
* For each page encountered it should:
* if the page is dirty, call file->writePage() to flush the page to disk
* and then set the dirty bit for the page to false, 
* remove the page from the hashtable (whether the page is clean or dirty) and
* invoke the Clear() method of BufDesc for the page frame.
* Throws PagePinnedException if some page of the file is pinned.
* Throws BadBuffer- Exception if an invalid page belonging to the file is encountered.
* @param file pointer
* @return nothing
*/
void BufMgr::flushFile(const File* file) 
{
  //firstly search where the corresponding frame it is 
  for (uint32_t i = 0; i < numBufs; i++) {
    FrameId fid = bufDescTable[i].frameNo;
    PageId pid = bufDescTable[i].pageNo;
    File* ff = bufDescTable[i].file;
    //if the filename is the same as the filename in this corresponding frame
    //then it means that we find out this file
    if (ff == file) {
      //Throws PagePinnedException if some page of the file is pinned. 
      if (bufDescTable[i].pinCnt > 0) {
        /*
         the use of pagepinnedexception is as following:
        explicit PagePinnedException(const std::string& nameIn, PageId pageNoIn, FrameId frameNoIn);
        Name of file that caused this exception. Page number in file. Frame number in buffer pool
        */
        throw PagePinnedException(file->filename(), pid, fid);
      }
      
      //Throws BadBuffer- Exception if an invalid page belonging to the file is encountered.
      if (bufDescTable[i].valid == false) {
        /*
         the use of badbufferexception is as following:
        explicit BadBufferException(FrameId frameNoIn, bool dirtyIn, bool validIn, bool refbitIn);
         * Frame number of bad buffer
         * True if buffer is dirty;  false otherwise
         * True if buffer is valid
         * Has this buffer frame been reference recently
        */
        throw BadBufferException(fid, bufDescTable[i].dirty, bufDescTable[i].valid, bufDescTable[i].refbit);
      }
      

      //(a) if the page is dirty, 
      if (bufDescTable[i].dirty) {
        //call file->writePage() to flush the page to disk 
        ff->writePage(bufPool[i]);
        //and then set the dirty bit for the page to false,
        bufDescTable[i].dirty = false;
      }
      
      //(b) remove the page from the hashtable
      //remove(const File* file, const PageId pageNo)
      //@throws HashNotFoundException if the page entry is not found in the hash table
      hashTable->remove(ff,pid);
      //(c) invoke the Clear() method of BufDesc for the page frame.
      bufDescTable[i].Clear();
    }
  }
}

/**
 * Get allocated page from given file and insert this page to the new allocated buffer frame and insert a new entry into hashtable.(used to map file and page number to buffer pool frames)
 * 
 * @param file pointer
 * @param page number that want to newly allocate
 * @param page pointer that want to newly allocate
 * @return nothing
 */
void BufMgr::allocPage(File* file, PageId &pageNo, Page*& page) 
{
  FrameId frameNo;
  
  // allocate a new page in file
  Page new_page = file->allocatePage();
  
  // allocate a buffer frame
  allocBuf(frameNo);
  
  // insert new page to buffer pool
  bufPool[frameNo] = new_page;
  
  pageNo = new_page.page_number();
  page = &bufPool[frameNo];
  
  // insert a new entry into hashtable
  hashTable->insert(file, pageNo, frameNo);
  
  // invoke Set()
  bufDescTable[frameNo].Set(file,pageNo);

}

/**
 * This method deletes the given page from file 
 * 
 * @param file pointer
 * @param page number that want to delete
 * @return nothing
 */
void BufMgr::disposePage(File* file, const PageId PageNo)
{
  FrameId frameNo;
  // check if the page to be deleted is allocated a frame in the buffer pool
  //  If so , return the corresponding frame number in frameNo
  hashTable->lookup(file,PageNo,frameNo);
  
  // free this frame if is allocated a frame in the buffer pool
  bufDescTable[frameNo].Clear();
  
  // remove correspondingentry from hash table
  hashTable->remove(file,PageNo);
  
  // delete this page from file
  file->deletePage(PageNo);

}

void BufMgr::printSelf(void) 
{
  BufDesc* tmpbuf;
  int validFrames = 0;
  
  for (std::uint32_t i = 0; i < numBufs; i++)
  {
    tmpbuf = &(bufDescTable[i]);
    std::cout << "FrameNo:" << i << " ";
    tmpbuf->Print();

    if (tmpbuf->valid == true)
      validFrames++;
  }

  std::cout << "Total Number of Valid Frames:" << validFrames << "\n";
}

}
