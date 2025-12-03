#pragma once

#include "mainwindow.h"
#include <QThread>
#include <QMetaObject>

// --- 线程安全辅助函数 ---

// 在主线程执行并返回结果 (阻塞等待)
template <typename Func>
auto runOnMain(MainWindow *win, Func &&f) -> decltype(f())
{
    using R = decltype(f());
    // 如果已经在主线程，直接执行
    if (QThread::currentThread() == win->thread())
    {
        return f();
    }
    // 否则调度到主线程
    R ret;
    QMetaObject::invokeMethod(win, [&]()
                              { ret = f(); }, Qt::BlockingQueuedConnection);
    return ret;
}

// 在主线程执行无返回值的函数 (阻塞等待)
template <typename Func>
void runOnMainVoid(MainWindow *win, Func &&f)
{
    if (QThread::currentThread() == win->thread())
    {
        f();
        return;
    }
    QMetaObject::invokeMethod(win, f, Qt::BlockingQueuedConnection);
}