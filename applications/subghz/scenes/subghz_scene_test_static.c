#include "../subghz_i.h"

void subghz_scene_test_static_on_enter(void* context) {
    SubGhz* subghz = context;
    view_dispatcher_switch_to_view(subghz->view_dispatcher, SubGhzViewStatic);
}

bool subghz_scene_test_static_on_event(void* context, SceneManagerEvent event) {
    // SubGhz* subghz = context;
    return false;
}

void subghz_scene_test_static_on_exit(void* context) {
    // SubGhz* subghz = context;
}