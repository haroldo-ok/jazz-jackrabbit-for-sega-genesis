#include <assert.h>
#include <stdio.h>
#include "stage_cheat.h"

int main(void) {
    unsigned char p=0;
    const unsigned char sequence[] = {CHEAT_UP,CHEAT_UP,CHEAT_DOWN,CHEAT_DOWN,CHEAT_LEFT,CHEAT_RIGHT,CHEAT_LEFT,CHEAT_RIGHT,CHEAT_B,CHEAT_A};
    int i;
    for (i=0;i<9;i++) assert(!stage_cheat_step(&p,sequence[i]));
    assert(stage_cheat_step(&p,sequence[9]) && p==0);
    assert(!stage_cheat_step(&p,CHEAT_UP));
    assert(!stage_cheat_step(&p,CHEAT_UP));
    assert(!stage_cheat_step(&p,CHEAT_UP)); /* overlapping prefix */
    for (i=2;i<9;i++) assert(!stage_cheat_step(&p,sequence[i]));
    assert(stage_cheat_step(&p,sequence[9]));
    assert(!stage_cheat_step(&p,CHEAT_UP));
    assert(!stage_cheat_step(&p,CHEAT_NONE) && p==1);
    assert(!stage_cheat_step(&p,CHEAT_OTHER) && p==0);
    for (i=1;i<10;i++) assert(!stage_cheat_step(&p,sequence[i]));
    puts("PASS: stage-select cheat and overlapping/rejected inputs");
    return 0;
}
