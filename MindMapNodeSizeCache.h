#pragma once

class MindMapNodeSizeCache {
public:
    bool isDirty() const { return m_dirty; }
    void invalidate() { m_dirty = true; }
    void markClean() { m_dirty = false; }
    bool consumeDirty() {
        if (!m_dirty) return false;
        m_dirty = false;
        return true;
    }

private:
    bool m_dirty = true;
};
