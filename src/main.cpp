#include "app/Application.hpp"

// Entry point. Construct the Application and run the render loop.
int main(int /*argc*/, char** /*argv*/) {
    utsyn::Application app;
    app.run();
    return 0;
}
