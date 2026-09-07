#include <QGuiApplication>
#include <QQmlApplicationEngine>
#include <QQmlContext>
#include "TestBridge.h"

int main(int argc, char *argv[])
{
    QGuiApplication app(argc, argv);

    TestBridge testBridge;
    QQmlApplicationEngine engine;

    // C++ sınıfı QML'e 'testBridge' adıyla bağlanır
    engine.rootContext()->setContextProperty("testBridge", &testBridge);

    engine.loadFromModule("TestArayuzu", "Main");
    return app.exec();
}