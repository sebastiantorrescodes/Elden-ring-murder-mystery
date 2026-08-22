#include "data.hpp"
#include "gpsbridge.hpp"

#include "api.h"

EROverlayAPI *api;

int init(EROverlayAPI *erOverlayAPI) {
    api = erOverlayAPI;

    er::minimap::gAtlas.load(api->getModulePath());
    er::minimap::gData.load();

    er::minimap::bridge::init();

    return 0;
}

void update() {
    er::minimap::gData.update();
    er::minimap::bridge::writeState();
}

int createRenderer(void *context, void *allocFunc, void *freeFunc, void *userData) {
    return 0;
}

void destroyRenderer() {
}

bool render() {
    // VERY IMPORTANT:
    // Do not draw anything into Elden Ring.
    return false;
}

void uninit() { er::minimap::bridge::shutdownBridge(); }

static PluginExports exports = {
    init,
    uninit,
    update,
    createRenderer,
    destroyRenderer,
    render
};

PLUGIN_DEFINE(exports)