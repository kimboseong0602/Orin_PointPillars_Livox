/****************************************************************************
** Meta object code from reading C++ file 'controlDetection.h'
**
** Created by: The Qt Meta Object Compiler version 67 (Qt 5.12.8)
**
** WARNING! All changes made in this file will be lost!
*****************************************************************************/

#include "../../../../../tools/camera-capture/controlDetection.h"
#include <QtCore/qbytearray.h>
#include <QtCore/qmetatype.h>
#if !defined(Q_MOC_OUTPUT_REVISION)
#error "The header file 'controlDetection.h' doesn't include <QObject>."
#elif Q_MOC_OUTPUT_REVISION != 67
#error "This file was generated using the moc from 5.12.8. It"
#error "cannot be used with the include files from this version of Qt."
#error "(The moc has changed too much.)"
#endif

QT_BEGIN_MOC_NAMESPACE
QT_WARNING_PUSH
QT_WARNING_DISABLE_DEPRECATED
struct qt_meta_stringdata_ControlDetectionWidget_t {
    QByteArrayData data[12];
    char stringdata0[139];
};
#define QT_MOC_LITERAL(idx, ofs, len) \
    Q_STATIC_BYTE_ARRAY_DATA_HEADER_INITIALIZER_WITH_OFFSET(len, \
    qptrdiff(offsetof(qt_meta_stringdata_ControlDetectionWidget_t, stringdata0) + ofs \
        - idx * sizeof(QByteArrayData)) \
    )
static const qt_meta_stringdata_ControlDetectionWidget_t qt_meta_stringdata_ControlDetectionWidget = {
    {
QT_MOC_LITERAL(0, 0, 22), // "ControlDetectionWidget"
QT_MOC_LITERAL(1, 23, 6), // "onSave"
QT_MOC_LITERAL(2, 30, 0), // ""
QT_MOC_LITERAL(3, 31, 8), // "onFreeze"
QT_MOC_LITERAL(4, 40, 7), // "toggled"
QT_MOC_LITERAL(5, 48, 11), // "onBoxRemove"
QT_MOC_LITERAL(6, 60, 10), // "onBoxClass"
QT_MOC_LITERAL(7, 71, 5), // "value"
QT_MOC_LITERAL(8, 77, 10), // "onBoxCoord"
QT_MOC_LITERAL(9, 88, 16), // "onQualityChanged"
QT_MOC_LITERAL(10, 105, 17), // "selectDatasetPath"
QT_MOC_LITERAL(11, 123, 15) // "selectLabelFile"

    },
    "ControlDetectionWidget\0onSave\0\0onFreeze\0"
    "toggled\0onBoxRemove\0onBoxClass\0value\0"
    "onBoxCoord\0onQualityChanged\0"
    "selectDatasetPath\0selectLabelFile"
};
#undef QT_MOC_LITERAL

static const uint qt_meta_data_ControlDetectionWidget[] = {

 // content:
       8,       // revision
       0,       // classname
       0,    0, // classinfo
       8,   14, // methods
       0,    0, // properties
       0,    0, // enums/sets
       0,    0, // constructors
       0,       // flags
       0,       // signalCount

 // slots: name, argc, parameters, tag, flags
       1,    0,   54,    2, 0x0a /* Public */,
       3,    1,   55,    2, 0x0a /* Public */,
       5,    0,   58,    2, 0x0a /* Public */,
       6,    1,   59,    2, 0x0a /* Public */,
       8,    1,   62,    2, 0x0a /* Public */,
       9,    1,   65,    2, 0x0a /* Public */,
      10,    0,   68,    2, 0x0a /* Public */,
      11,    0,   69,    2, 0x0a /* Public */,

 // slots: parameters
    QMetaType::Void,
    QMetaType::Void, QMetaType::Bool,    4,
    QMetaType::Void,
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void, QMetaType::Double,    7,
    QMetaType::Void, QMetaType::Int,    7,
    QMetaType::Void,
    QMetaType::Void,

       0        // eod
};

void ControlDetectionWidget::qt_static_metacall(QObject *_o, QMetaObject::Call _c, int _id, void **_a)
{
    if (_c == QMetaObject::InvokeMetaMethod) {
        auto *_t = static_cast<ControlDetectionWidget *>(_o);
        Q_UNUSED(_t)
        switch (_id) {
        case 0: _t->onSave(); break;
        case 1: _t->onFreeze((*reinterpret_cast< bool(*)>(_a[1]))); break;
        case 2: _t->onBoxRemove(); break;
        case 3: _t->onBoxClass((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 4: _t->onBoxCoord((*reinterpret_cast< double(*)>(_a[1]))); break;
        case 5: _t->onQualityChanged((*reinterpret_cast< int(*)>(_a[1]))); break;
        case 6: _t->selectDatasetPath(); break;
        case 7: _t->selectLabelFile(); break;
        default: ;
        }
    }
}

QT_INIT_METAOBJECT const QMetaObject ControlDetectionWidget::staticMetaObject = { {
    &QWidget::staticMetaObject,
    qt_meta_stringdata_ControlDetectionWidget.data,
    qt_meta_data_ControlDetectionWidget,
    qt_static_metacall,
    nullptr,
    nullptr
} };


const QMetaObject *ControlDetectionWidget::metaObject() const
{
    return QObject::d_ptr->metaObject ? QObject::d_ptr->dynamicMetaObject() : &staticMetaObject;
}

void *ControlDetectionWidget::qt_metacast(const char *_clname)
{
    if (!_clname) return nullptr;
    if (!strcmp(_clname, qt_meta_stringdata_ControlDetectionWidget.stringdata0))
        return static_cast<void*>(this);
    return QWidget::qt_metacast(_clname);
}

int ControlDetectionWidget::qt_metacall(QMetaObject::Call _c, int _id, void **_a)
{
    _id = QWidget::qt_metacall(_c, _id, _a);
    if (_id < 0)
        return _id;
    if (_c == QMetaObject::InvokeMetaMethod) {
        if (_id < 8)
            qt_static_metacall(this, _c, _id, _a);
        _id -= 8;
    } else if (_c == QMetaObject::RegisterMethodArgumentMetaType) {
        if (_id < 8)
            *reinterpret_cast<int*>(_a[0]) = -1;
        _id -= 8;
    }
    return _id;
}
QT_WARNING_POP
QT_END_MOC_NAMESPACE
