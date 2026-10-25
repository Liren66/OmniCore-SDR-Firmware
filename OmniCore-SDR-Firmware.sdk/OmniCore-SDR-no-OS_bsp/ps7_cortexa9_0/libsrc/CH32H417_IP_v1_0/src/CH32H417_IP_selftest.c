#include "CH32H417_IP.h"

/* The generated four-RW-register test is invalid for this streaming core:
 * STATUS0/1 are read-only and CTRL writes can enable/reset the datapath.
 * There is no hardware identification/scratch register for a safe self-test.
 * Use GetStatus plus a controlled end-to-end transfer test instead.
 */
XStatus CH32H417_IP_Reg_SelfTest(void *baseaddr_p)
{
    (void)baseaddr_p;
    return XST_NO_FEATURE;
}
