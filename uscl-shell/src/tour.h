#pragma once

#include <QString>

class QObject;

// uscl --qml-tour <folder>: screenshots of every menu in every theme (tour.cpp)
void startTour(QObject* root, const QString& dir);
