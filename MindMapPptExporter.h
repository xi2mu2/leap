#pragma once

#include <QString>

class QGraphicsScene;

class MindMapPptExporter {
public:
    static bool exportToPptx(QGraphicsScene* scene, const QString& filePath, QString* errorMessage = nullptr);
};
