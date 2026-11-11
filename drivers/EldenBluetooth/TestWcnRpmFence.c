/* SPDX-License-Identifier: MIT */
#define main upstream_rpmh_fixture_main
#include "test_sm8750_rpmh.c"
#undef main
int main(void){
    const Sm8750RpmhCmd commands[]={{0x40000,1312},{0x40008,7},{0x40004,1}};
    Rsc *m=rsc_new(3,3);Sm8750RpmhIo io=rsc_io(m);Sm8750RpmhResult result;
    CHECK(!Sm8750RpmhWriteActive(&io,commands,3,7,&result));
    CHECK(Sm8750RpmhDrv2Owner()==0&&m->nsent==1);
    free(m);
    m=rsc_new(3,3);io=rsc_io(m);m->complete_ms=-1;
    CHECK(Sm8750RpmhWriteActive(&io,commands,3,7,&result)==Sm8750RpmhTimeout);
    CHECK(Sm8750RpmhDrv2Owner()!=0&&m->triggered);
    CHECK(reg_of(m,tcs(result.tcs)+m->control)==0x01010000);
    CHECK(!(reg_of(m,Sm8750RpmhTcsBase)&(1u<<result.tcs)));
    unsigned sends=m->nsent;
    CHECK(Sm8750RpmhWriteActive(&io,commands,3,7,&result)==Sm8750RpmhBusy&&m->nsent==sends);
    // Test teardown only. Production may release only after a real reset.
    Sm8750RpmhDrv2Release();free(m);
    puts("PASS completed and timed-out RSC ownership fence");return 0;
}
