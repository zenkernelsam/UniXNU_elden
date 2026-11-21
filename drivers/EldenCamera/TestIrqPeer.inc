/* SPDX-License-Identifier: BSD-2-Clause-Patent */
/* Host-only XNU wait/IRQ scheduling peer. Production wait and latch methods
 * are compiled unchanged. No camera pixels or physical readiness supplied. */
using thread_t=void *;using event_t=void *;
enum {THREAD_UNINT=1,THREAD_AWAKENED=2,kMillisecondScale=1};
static thread_t THREAD_NULL=nullptr;static auto THREAD_CONTINUE_NULL=nullptr;
static thread_t camera_test_thread=(void *)1;static unsigned camera_test_wakes,camera_test_waits,camera_test_blocks,camera_test_clears,camera_test_refs;
static bool camera_test_enrolled;static void (*camera_test_wait_hook)();static void (*camera_test_block_hook)();
static thread_t current_thread(){return camera_test_thread;}
static void thread_reference(thread_t t){CHECK(t&&t==camera_test_thread);camera_test_refs++;}
static void thread_wakeup(event_t event){CHECK(event);camera_test_wakes++;}
static int assert_wait_timeout(event_t event,unsigned mode,unsigned ms,unsigned scale){
    CHECK(event&&mode==THREAD_UNINT&&ms&&ms<=5000&&scale==kMillisecondScale&&!camera_test_enrolled);
    camera_test_waits++;camera_test_enrolled=true;if(camera_test_wait_hook)camera_test_wait_hook();return 0;
}
static int clear_wait(thread_t thread,unsigned result){CHECK(thread==camera_test_thread&&result==THREAD_AWAKENED&&camera_test_enrolled);
    camera_test_clears++;camera_test_enrolled=false;return 0;}
static int thread_block(decltype(nullptr) continuation){CHECK(continuation==THREAD_CONTINUE_NULL&&camera_test_enrolled);
    camera_test_blocks++;if(camera_test_block_hook)camera_test_block_hook();camera_test_enrolled=false;return 0;}
