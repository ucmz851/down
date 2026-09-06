#include "update.h"
#include <assert.h>
#include <stdio.h>

int main(void) {
    printf("[*] Running test_update...\n");

    /* Test version comparisons */
    assert(version_compare("0.0.1", "0.0.1") == 0);
    assert(version_compare("v0.0.1", "0.0.1") == 0);
    assert(version_compare("0.0.1", "v0.0.1") == 0);
    assert(version_compare("v1.0.0", "v0.9.9") > 0);
    assert(version_compare("0.0.2", "0.0.1") > 0);
    assert(version_compare("0.0.1", "0.0.2") < 0);
    assert(version_compare("0.1.0", "0.0.9") > 0);
    assert(version_compare("1.0.0", "0.99.99") > 0);

    printf("[+] test_update passed successfully!\n");
    return 0;
}
