/* harness_main.cpp -- runs the real RZNAI_AGI model on real foveal input.
 *
 * Links the patched RZNAI_AGI.cpp against this engine, supplying in_0 / in_1
 * from the foveal stereo stream instead of the simulation stubs, and driving
 * cycle() for a bounded number of cycles.
 *
 * Build with:
 *   -DRZNAI_AGI_EXTERNAL_SENSORS   leave out the stub sensors
 *   -DRZNAI_AGI_NO_MAIN            leave out the model's own main()
 *   -DRZNAI_AGI_MAX_CYCLES=N       bound the run
 *
 * See harness/build_harness.bat.
 *
 * What this measures: whether foveal stereo words actually reach the model,
 * and whether the model's behaviour responds to them at all.  The model picks
 * the next sensor from its own output, so if its output never varies, the
 * requested-sensor histogram collapses to a single value -- an observable
 * proxy for "the input is not influencing the network", requiring no extra
 * instrumentation inside the model.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>

#include "RZNAI_AGI.hpp"

extern "C" {
#include "rzn_agi_bridge.h"
#include "rzn_pack.h"
}

/* The model's sensor front end, replacing the simulation stubs. */
__int32 in_0() { return rzn_bridge_read(RZN_SENSOR_LEFT); }
__int32 in_1() { return rzn_bridge_read(RZN_SENSOR_RIGHT); }

int main(int argc, char **argv)
{
    int32_t w = 64, h = 48, sx = -1, sy = -1;
    int i;

    for (i = 1; i < argc; i++) {
        if (!strcmp(argv[i], "-w") && i + 1 < argc) w = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-h") && i + 1 < argc) h = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-x") && i + 1 < argc) sx = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-y") && i + 1 < argc) sy = atoi(argv[++i]);
        else {
            printf("usage: rzn_harness [-w W] [-h H] [-x X] [-y Y]\n");
            return 2;
        }
    }
    if (sx < 0) sx = w / 2;
    if (sy < 0) sy = h / 2;

    /* Unbuffered: if the model faults mid-run we still see how far it got. */
    setvbuf(stdout, NULL, _IONBF, 0);

    printf("RZN AI foveal stereo -> RZNAI_AGI integration harness\n");
    printf("  sensor        %d x %d\n", w, h);
    printf("  seed          (%d, %d)\n", sx, sy);
    printf("  packing       profile %d\n", RZN_PACK_PROFILE);
    printf("  cycle limit   %d\n", (int)RZNAI_AGI_MAX_CYCLES);

    if (!rzn_bridge_open(w, h, sx, sy)) {
        printf("error: could not open the foveal source\n");
        return 1;
    }

    {
        AGI_Sys *stm = instantiate();
        if (!stm) {
            printf("error: instantiate() returned null\n");
            return 1;
        }
        printf("  model         in_sz=%d In_Q_ct=%d sensory_bits=%d "
               "hidden_sz=%d hidden_ct=%d\n",
               (int)stm->in_sz, (int)stm->In_Q_ct, (int)stm->sensory_bits,
               (int)stm->hidden_sz, (int)stm->hidden_ct);
        printf("\nrunning...\n");

        cycle(stm);

        printf("\n--- what the model consumed ---\n");
        {
            const rzn_bridge_stats *s = rzn_bridge_get_stats();
            printf("  readings served     %llu\n",
                   (unsigned long long)s->readings);
            printf("    index words       %llu\n",
                   (unsigned long long)s->index_words);
            printf("    left  colour      %llu\n",
                   (unsigned long long)s->left_words);
            printf("    right colour      %llu\n",
                   (unsigned long long)s->right_words);
            printf("    disparity         %llu\n",
                   (unsigned long long)s->disp_words);
            printf("  frames rendered     %llu\n",
                   (unsigned long long)s->frames_pushed);
            printf("  engine words out    %lld\n",
                   (long long)rzn_bridge_words_emitted());
            printf("  underruns           %llu\n",
                   (unsigned long long)s->underruns);
            printf("\n--- sensor selection (driven by the model's output) ---\n");
            printf("  asked for left      %llu\n",
                   (unsigned long long)s->asked_left);
            printf("  asked for right     %llu\n",
                   (unsigned long long)s->asked_right);
            printf("  request vs word tag %llu disagreements\n",
                   (unsigned long long)s->sensor_mismatch);

            if (s->readings > 0 &&
                (s->asked_left == 0 || s->asked_right == 0)) {
                printf("\n  NOTE: the model requested the same sensor on every\n"
                       "  cycle. Its output is not varying with the input.\n");
            }
        }

        printf("\n--- model state after the run ---\n");
        printf("  Current_Input       %d (0x%08x)\n",
               (int)stm->Current_Input, (unsigned)stm->Current_Input);
        printf("  Input_Queue        ");
        for (i = 0; i < stm->In_Q_ct; i++)
            printf(" %08x", (unsigned)stm->Input_Queue[i]);
        printf("\n");
        printf("  knowledge bank sz   %d\n", (int)stm->kbsz);
        printf("  rewards / disinc    %d / %d\n",
               (int)stm->rwtop + 1, (int)stm->dvtop + 1);
    }

    rzn_bridge_close();
    return 0;
}
