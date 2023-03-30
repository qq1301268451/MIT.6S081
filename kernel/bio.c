// Buffer cache.
//
// The buffer cache is a linked list of buf structures holding
// cached copies of disk block contents.  Caching disk blocks
// in memory reduces the number of disk reads and also provides
// a synchronization point for disk blocks used by multiple processes.
//
// Interface:
// * To get a buffer for a particular disk block, call bread.
// * After changing buffer data, call bwrite to write it to disk.
// * When done with the buffer, call brelse.
// * Do not use the buffer after calling brelse.
// * Only one process at a time can use a buffer,
//     so do not keep them longer than necessary.


#include "types.h"
#include "param.h"
#include "spinlock.h"
#include "sleeplock.h"
#include "riscv.h"
#include "defs.h"
#include "fs.h"
#include "buf.h"

struct {
  struct spinlock lock;
  struct buf buf[NBUF];

  // Linked list of all buffers, through prev/next.
  // Sorted by how recently the buffer was used.
  // head.next is most recent, head.prev is least.
  //struct buf head;
  //struct buf *table[NBUCKET] //hashmap 快速查找块
} bcache[NBUCKET];

uint idx(uint blockno)//计算哈希索引；
{
   return blockno % NBUCKET;
}

void
binit(void)
{
  struct buf *b;
  
  for(int i=0;i<NBUCKET;i++) 
  {  
    for(b = bcache[i].buf; b < bcache[i].buf+NBUF; b++){ 
      initsleeplock(&b->lock, "buffer");
    }
    initlock(&bcache[i].lock, "bcache.bucket");
  }
}

// Look through buffer cache for block on device dev.
// If not found, allocate a buffer.
// In either case, return locked buffer.
static struct buf*
bget(uint dev, uint blockno)
{
  struct buf *b , *min_b = 0;
  int index = idx(blockno);
  uint mintime = -1;
  acquire(&bcache[index].lock);

  // Is the block already cached?
  for(b = bcache[index].buf; b < bcache[index].buf+NBUF ; b++ ){
    if( b->dev == dev && b->blockno == blockno){
      b->refcnt++;
      release(&bcache[index].lock);
      acquiresleep(&b->lock);
      return b;
    }
    if( b->refcnt == 0 && b->timeticks < mintime){
        mintime = b->timeticks;
        min_b = b;
    }
  }
  if( min_b != 0 ){
    b = min_b;
 	  b->dev = dev;
    b->blockno = blockno;
    b->valid = 0;
    b->refcnt = 1;
	  release(&bcache[index].lock);
    acquiresleep(&b->lock);
    return b;   
  } 
  release(&bcache[index].lock);
  panic("bget: no buffers");
}

// Return a locked buf with the contents of the indicated block.
struct buf*
bread(uint dev, uint blockno)
{
  struct buf *b;

  b = bget(dev, blockno);
  if(!b->valid) {
    virtio_disk_rw(b, 0);
    b->valid = 1;
  }
  return b;
}

// Write b's contents to disk.  Must be locked.
void
bwrite(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("bwrite");
  virtio_disk_rw(b, 1);
}

// Release a locked buffer.
// Move to the head of the most-recently-used list.
void
brelse(struct buf *b)
{
  if(!holdingsleep(&b->lock))
    panic("brelse");

  releasesleep(&b->lock);
  int index=idx(b->blockno);
  acquire(&bcache[index].lock);
  b->refcnt--;
  if (b->refcnt == 0) {
    // no one is waiting for it.
    b->timeticks = ticks;
  }
  release(&bcache[index].lock);
}

void
bpin(struct buf *b) {
  int index = idx(b->blockno);
  acquire(&bcache[index].lock);
  b->refcnt++;
  release(&bcache[index].lock);
}

void
bunpin(struct buf *b) {
  int index = idx(b->blockno);
  acquire(&bcache[index].lock);
  b->refcnt--;
  release(&bcache[index].lock);
}


