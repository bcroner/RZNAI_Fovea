/* init_experiment.cpp -- does the model's output respond to its input?
 *
 * A 50000-cycle run had the model request the same sensor on 49999 cycles.
 * The hypothesis under test is that this is the weight/target initialisation
 * in instantiate(), not a defect in perform_iann().
 *
 * Every seeded value depends only on the fan-out index j, never on the source
 * unit i:
 *
 *     input_weights[i][j] = (j % 2 == 0 ? -16384 : 16384);
 *     input_targets[i][j] =  j % hidden_sz;
 *
 * so each source contributes an identical vector, and which source was active
 * cannot affect the result -- only how many were.
 *
 * This program calls perform_iann() directly with controlled Input_Queue
 * contents, so it measures the network in isolation without cycle() in the
 * way, and rewrites the weights and targets in place after instantiate().
 * The model source is not modified.
 */

#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <set>
#include <vector>

#include "RZNAI_AGI.hpp"

extern "C" {
#include "rzn_agi_bridge.h"
#include "rzn_pack.h"
}

/* Deterministic, so runs are comparable. */
static unsigned h2(unsigned a, unsigned b)
{
    unsigned h = a * 374761393u + b * 668265263u;
    h = (h ^ (h >> 13)) * 1274126177u;
    return h ^ (h >> 16);
}

/* Target and weight must be drawn independently.  hidden_sz is even, so
 * r % hidden_sz has the same parity as r -- deriving a target from r and a
 * weight sign from r & 1 locks every even-numbered unit to negative weights
 * and every odd-numbered unit to positive, which makes the network degenerate
 * for reasons that have nothing to do with the model. */
static unsigned h2w(unsigned a, unsigned b)
{
    return h2(a ^ 0x9E3779B9u, b + 0x85EBCA6Bu);
}

enum Scheme {
    SHIPPED = 0,
    TARGETS_BY_SOURCE,
    WEIGHTS_BY_SOURCE,
    BOTH_BY_SOURCE,
    BOTH_GRADED,
    SCHEME_COUNT
};

static const char *scheme_name(int s)
{
    switch (s) {
    case SHIPPED:           return "shipped (control)";
    case TARGETS_BY_SOURCE: return "targets vary by source";
    case WEIGHTS_BY_SOURCE: return "weights vary by source";
    case BOTH_BY_SOURCE:    return "both vary by source";
    case BOTH_GRADED:       return "both vary + graded magnitudes";
    }
    return "?";
}

/* Rewrite the network's weights and targets in place. */
static void apply_scheme(AGI_Sys *stm, int scheme)
{
    const __int32 in_units = stm->in_sz * stm->In_Q_ct;
    const __int32 fan      = stm->hidden_sz >> 1;

    if (scheme == SHIPPED)
        return;                       /* leave instantiate()'s values alone */

    for (__int32 i = 0; i < in_units; i++)
        for (__int32 j = 0; j < fan; j++) {
            unsigned r  = h2((unsigned)i, (unsigned)j);
            unsigned rw = h2w((unsigned)i, (unsigned)j);
            switch (scheme) {
            case TARGETS_BY_SOURCE:
                stm->input_targets[i][j] = (__int32)(r % (unsigned)stm->hidden_sz);
                break;
            case WEIGHTS_BY_SOURCE:
                stm->input_weights[i][j] = (rw & 1u) ? 16384 : -16384;
                break;
            case BOTH_BY_SOURCE:
                stm->input_targets[i][j] = (__int32)(r % (unsigned)stm->hidden_sz);
                stm->input_weights[i][j] = (rw & 1u) ? 16384 : -16384;
                break;
            case BOTH_GRADED:
                stm->input_targets[i][j] = (__int32)(r % (unsigned)stm->hidden_sz);
                stm->input_weights[i][j] = (__int32)(rw % 32768u) - 16384;
                break;
            }
        }

    for (__int32 c = 0; c < stm->hidden_ct; c++)
        for (__int32 i = 0; i < stm->hidden_sz; i++)
            for (__int32 j = 0; j < fan; j++) {
                unsigned r  = h2((unsigned)(c * 7919 + i), (unsigned)j);
                unsigned rw = h2w((unsigned)(c * 7919 + i), (unsigned)j);
                switch (scheme) {
                case TARGETS_BY_SOURCE:
                    stm->hidden[c]->targets[i][j] =
                        (__int32)(r % (unsigned)stm->hidden_sz);
                    break;
                case WEIGHTS_BY_SOURCE:
                    stm->hidden[c]->weights[i][j] = (rw & 1u) ? 16384 : -16384;
                    break;
                case BOTH_BY_SOURCE:
                    stm->hidden[c]->targets[i][j] =
                        (__int32)(r % (unsigned)stm->hidden_sz);
                    stm->hidden[c]->weights[i][j] = (rw & 1u) ? 16384 : -16384;
                    break;
                case BOTH_GRADED:
                    stm->hidden[c]->targets[i][j] =
                        (__int32)(r % (unsigned)stm->hidden_sz);
                    stm->hidden[c]->weights[i][j] = (__int32)(rw % 32768u) - 16384;
                    break;
                }
            }

    for (__int32 i = 0; i < stm->hidden_sz; i++)
        for (__int32 j = 0; j < (stm->out_sz >> 1); j++) {
            unsigned r  = h2((unsigned)i + 104729u, (unsigned)j);
            unsigned rw = h2w((unsigned)i + 104729u, (unsigned)j);
            switch (scheme) {
            case TARGETS_BY_SOURCE:
                stm->output_targets[i][j] = (__int32)(r % (unsigned)stm->out_sz);
                break;
            case WEIGHTS_BY_SOURCE:
                stm->output_weights[i][j] = (rw & 1u) ? 16384 : -16384;
                break;
            case BOTH_BY_SOURCE:
                stm->output_targets[i][j] = (__int32)(r % (unsigned)stm->out_sz);
                stm->output_weights[i][j] = (rw & 1u) ? 16384 : -16384;
                break;
            case BOTH_GRADED:
                stm->output_targets[i][j] = (__int32)(r % (unsigned)stm->out_sz);
                stm->output_weights[i][j] = (__int32)(rw % 32768u) - 16384;
                break;
            }
        }
}

static unsigned firing_hash(const AGI_Sys *stm, __int32 layer)
{
    unsigned h = 2166136261u;
    for (__int32 i = 0; i < stm->hidden_sz; i++) {
        h ^= (unsigned)(stm->hidden[layer]->firings[i] ? 1u : 0u);
        h *= 16777619u;
    }
    return h;
}

int main(int argc, char **argv)
{
    int trials = 400;
    int sensor_bit = 0;
    int32_t w = 64, h = 48;

    if (argc > 1) trials = atoi(argv[1]);
    if (argc > 2) sensor_bit = atoi(argv[2]) & 1;

    setvbuf(stdout, NULL, _IONBF, 0);

    printf("Does the model's output respond to its input?\n");
    printf("  %d real foveal inputs per scheme, sensor bit %d, "
           "perform_iann() called directly\n\n", trials, sensor_bit);

    /* Collect real foveal words once, so every scheme sees identical input. */
    int32_t *words = (int32_t *)malloc(sizeof(int32_t) * (size_t)trials);
    if (!rzn_bridge_open(w, h, w / 2, h / 2)) {
        printf("error: foveal source failed to open\n");
        return 1;
    }
    for (int i = 0; i < trials; i++) {
        /* read_sensory() builds (reading << sensory_bits | sensor) << 1 | 0.
         * The sensor bit is whichever camera the model asked for, so it is
         * part of the input the network sees -- and in a live cycle() run the
         * model asks for sensor 1 almost always. Pass it on the command line
         * so both regimes can be reproduced. */
        int32_t reading = rzn_bridge_read(RZN_SENSOR_LEFT);
        words[i] = (int32_t)(((unsigned)reading << 1 | (unsigned)sensor_bit)
                             << 1 | 0u);
    }
    rzn_bridge_close();

    {
        std::set<int32_t> distinct_in(words, words + trials);
        printf("  the %d inputs contain %u distinct values\n\n",
               trials, (unsigned)distinct_in.size());
    }

    printf("%-32s %10s %12s %12s  %s\n",
           "scheme", "outputs", "layer-0 pats", "final pats", "varying bits");
    printf("---------------------------------------------------------------"
           "------------------------\n");

    for (int s = 0; s < SCHEME_COUNT; s++) {
        AGI_Sys *stm = instantiate();
        apply_scheme(stm, s);

        std::set<__int32> outs;
        std::set<unsigned> l0, lf;
        std::vector<__int32> first_n;

        for (int i = 0; i < trials; i++) {
            for (__int32 q = stm->In_Q_ct - 1; q >= 1; q--)
                stm->Input_Queue[q] = stm->Input_Queue[q - 1];
            stm->Input_Queue[0] = words[i];
            stm->Current_Input = words[i];

            __int32 out = perform_iann(stm);
            outs.insert(out);
            first_n.push_back(out);
            l0.insert(firing_hash(stm, 0));
            lf.insert(firing_hash(stm, stm->hidden_ct - 1));
        }

        /* Which output bits actually move?  cycle() derives the next sensor
         * from bit 1 alone (`sensor = (output >> 1) & sensor_mask`) and the
         * recall flag from bit 0, so a bit that never changes is a channel
         * the model cannot act on -- regardless of how many distinct output
         * values there are. */
        char bits[64];
        int p = 0;
        for (int b = 0; b < 8; b++) {
            int seen0 = 0, seen1 = 0;
            for (std::set<__int32>::iterator it = outs.begin();
                 it != outs.end(); ++it)
                ((*it >> b) & 1) ? (seen1 = 1) : (seen0 = 1);
            if (seen0 && seen1) p += sprintf(bits + p, "%d ", b);
        }
        if (p == 0) { strcpy(bits, "none"); }

        printf("%-32s %10u %12u %12u  %s\n", scheme_name(s),
               (unsigned)outs.size(), (unsigned)l0.size(),
               (unsigned)lf.size(), bits);

        printf("%-32s   values:", "");
        int shown = 0;
        for (std::set<__int32>::iterator it = outs.begin();
             it != outs.end() && shown < 20; ++it, ++shown)
            printf(" %d", (int)*it);
        if (outs.size() > 20) printf(" ...");
        printf("\n");

        /* The aggregate hides *when* the variation appears.  cycle() acts on
         * bit 1 every cycle, so a long constant prefix means the model behaves
         * identically for that whole stretch even though the set of outputs
         * over the full run is diverse. */
        printf("%-32s   first 24:", "");
        for (int k = 0; k < 24 && k < (int)first_n.size(); k++)
            printf(" %d", (int)first_n[k]);
        printf("\n");
        {
            size_t k = 1;
            while (k < first_n.size() && ((first_n[k] >> 1) & 1) ==
                                         ((first_n[0] >> 1) & 1))
                k++;
            if (k >= first_n.size())
                printf("%-32s   bit 1 never changes across all %u trials\n",
                       "", (unsigned)first_n.size());
            else
                printf("%-32s   bit 1 first changes at trial %u\n",
                       "", (unsigned)k);
        }
    }

    printf("\n'outputs' is how many distinct values perform_iann() returned;\n"
           "1 means the network cannot respond to its input at all.\n"
           "'varying bits' is which output bits actually move. cycle() reads\n"
           "the next sensor from bit 1 and the recall flag from bit 0, so if\n"
           "those are absent the model's behaviour cannot change even when\n"
           "the output value does.\n");

    free(words);
    return 0;
}
