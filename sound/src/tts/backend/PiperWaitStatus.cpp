#include "tts/backend/PiperWaitStatus.hpp"

#include <iostream>
#include <sys/wait.h>

namespace hecquin::tts::backend {

bool log_piper_wait_status(int status) {
    if (WIFEXITED(status)) {
        const int rc = WEXITSTATUS(status);
        if (rc == 0) return true;
        std::cerr << "[piper] exited with " << rc << std::endl;
        return false;
    }
    if (WIFSIGNALED(status)) {
        std::cerr << "[piper] killed by signal " << WTERMSIG(status) << std::endl;
        return false;
    }
    return false;
}

} // namespace hecquin::tts::backend
