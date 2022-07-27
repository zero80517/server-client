#include "client.h"

#include <QApplication>

int main(int argc, char *argv[])
{
    QApplication a(argc, argv);
    Client w("localhost", 2323);
    w.show();
    return a.exec();
}
