#include "../MindMapNodeSizeCache.h"

#include <QDebug>

#define CHECK(condition) do { if (!(condition)) { qCritical() << "Failed:" << #condition; return 1; } } while (false)

int main() {
    MindMapNodeSizeCache cache;
    CHECK(cache.isDirty());
    CHECK(cache.consumeDirty());
    CHECK(!cache.isDirty());
    CHECK(!cache.consumeDirty());

    cache.invalidate();
    CHECK(cache.isDirty());
    CHECK(cache.consumeDirty());
    CHECK(!cache.isDirty());
    CHECK(!cache.consumeDirty());
    return 0;
}
