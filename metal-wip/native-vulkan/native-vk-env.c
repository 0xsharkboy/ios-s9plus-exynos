/* Run the existing Metal API tests with PanVK's explicit experimental opt-in.
 * Scion's rc.jobs executor supplies its own envp, discarding launchd's environment.
 * Keep this setting confined to the test process and its children.
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

int main(int argc, char **argv)
{
    if (argc != 3 || strcmp(argv[1], "/redwood/bin/metal_test") != 0 ||
        (strcmp(argv[2], "objects") != 0 && strcmp(argv[2], "blits") != 0)) {
        fputs("NATIVE_VK_ENV: expected metal_test objects or blits\n", stderr);
        return 2;
    }
    if (setenv("PAN_I_WANT_A_BROKEN_VULKAN_DRIVER", "1", 1) ||
        setenv("MESA_LOG", "file", 1) || setenv("MESA_LOG_LEVEL", "debug", 1) ||
        setenv("PANVK_DEBUG", "startup", 1)) {
        perror("NATIVE_VK_ENV setenv");
        return 1;
    }
    printf("NATIVE_VK_ENV: PanVK opt-in set before exec of %s %s\n", argv[1], argv[2]);
    fflush(stdout);
    execv(argv[1], argv + 1);
    perror("NATIVE_VK_ENV execv");
    return 127;
}
