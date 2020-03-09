/*
*    MIT License
*
*    Copyright (c) 2019 Zakhar Bondia
*
*    Permission is hereby granted, free of charge, to any person obtaining a copy
*    of this software and associated documentation files (the "Software"), to deal
*    in the Software without restriction, including without limitation the rights
*    to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
*    copies of the Software, and to permit persons to whom the Software is
*    furnished to do so, subject to the following conditions:
*
*    The above copyright notice and this permission notice shall be included in all
*    copies or substantial portions of the Software.
*
*    THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
*    IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
*    FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
*    AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
*    LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
*    OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN THE
*    SOFTWARE.
*/

#pragma once
#include <QDebug>
#include <QString>
#include <QtCore>
#include <QtDBus>

#include <type_traits>
#include <exception>

#include "boost/preprocessor.hpp"

#include "wobjectdefs.h"
#include "wobjectimpl.h"

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// PUBLIC MACRO ////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

#define HARDBUS_DEFINE_SERVICE(TAG, INTERFACE, API, BUS_SERVICE, BUS_PATH, BUS_INTERFACE, BUS_TYPE) \
    struct TAG \
    { \
        HARDBUS_INTERNAL_DEFINE_TRAITS_( \
            TAG, INTERFACE, BUS_SERVICE, BUS_PATH, BUS_INTERFACE, BUS_TYPE) \
        HARDBUS_INTERNAL_DEFINE_EXPORT_ADAPTOR_(TAG, BUS_INTERFACE, API) \
        HARDBUS_INTERNAL_DEFINE_IMPORT_ADAPTOR_(TAG, API) \
        HARDBUS_INTERNAL_DEFINE_ACCESS_ADAPTOR_(TAG, API) \
        static const char *ServiceName() {return BUS_SERVICE;} \
        static const char *ServicePath() {return BUS_PATH;} \
        static const char *ServiceInterface() {return BUS_INTERFACE;} \
        static QDBusConnection Connection() {return BUS_TYPE;} \
        static void RegisterService(INTERFACE *service) { new ExporAdaptorFor##TAG(service); } \
        static INTERFACE *CreateServiceInterface(QObject *parent = nullptr) \
        { \
            return new AccessFor##TAG{parent}; \
        } \
        static bool WaitAndConnectService(INTERFACE *service) \
        { \
            hardbus::WaitForServiceRegistration(BUS_SERVICE, BUS_TYPE); \
            auto casted = qobject_cast<AccessFor##TAG *>(service); \
            if (!casted) { \
                qWarning() << "Wrong instance to connect to" << BUS_SERVICE; \
                return false; \
            } \
            if (casted->dbus_interface_ != nullptr) { \
                qWarning() << "Can't reconnect previously connected service " << BUS_SERVICE; \
                return false; \
            } \
            casted->dbus_interface_ = new ImportAdaptorFor##TAG{casted}; \
            return true; \
        } \
        static INTERFACE *CreateAndConnectService(QObject *parent = nullptr) \
        { \
            auto service = CreateServiceInterface(parent); \
            WaitAndConnectService(service);\
            return service; \
        } \
    };

#define HARDBUS_DEFINE_SERVICE_IMPL(TAG) \
    W_OBJECT_IMPL(TAG::ExporAdaptorFor##TAG); \
    W_OBJECT_IMPL(TAG::ImportAdaptorFor##TAG); \
    W_OBJECT_IMPL(TAG::AccessFor##TAG);

///////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////// PUBLIC ///////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

namespace hardbus
{
//! Customization point. Defines how types are converted to string and back
//! Must provide QString ToString(T) and T FromString() methods
template<class T, class = void>
struct ProxyStringConverter;

//! Exports \a service to the worlds
template<class Traits>
void RegisterService(typename Traits::interface *service, Traits = {})
{
    new typename Traits::dbus_export(service);
}

//! Creates a service access object
template<class Traits>
auto CreateServiceInterface(Traits = {}, QObject *parent = nullptr) -> typename Traits::interface *
{
    return new typename Traits::access{parent};
}

//Checks if service
inline bool IsServiceRegistered(const QString &service_name, const QDBusConnection &connection)
{
    bool is_registered = connection.interface()->isServiceRegistered(service_name);
    return is_registered;
}

template<class Traits>
bool IsServiceRegistered(Traits = {})
{
    return IsServiceRegistered(Traits::dbus_service_name, Traits::connection_type());
}

inline bool WaitForServiceRegistration(const QString &service_name,
                                       const QDBusConnection &connection)
{
    if (IsServiceRegistered(service_name, connection)) {
        return true;
    }

    QDBusServiceWatcher watcher(service_name, connection);
    QEventLoop loop;
    QObject::connect(&watcher, &QDBusServiceWatcher::serviceRegistered, &loop, &QEventLoop::quit);
    loop.exec();
    return true;
}

template<class Traits>
bool WaitForServiceRegistration(Traits = {})
{
    return WaitForServiceRegistration(Traits::dbus_service_name, Traits::connection_type());
}

template<class Traits>
bool ConnectService(typename Traits::interface *service, Traits = {})
{
    auto service_name = Traits::dbus_service_name;
    if (!IsServiceRegistered<Traits>()) {
        qWarning() << "Service" << service_name << "is not registered";
        return false;
    }
    auto casted = qobject_cast<typename Traits::access *>(service);
    if (!casted) {
        qWarning() << "Wrong instance to connect to" << service_name;
        return false;
    }
    if (casted->dbus_interface_ != nullptr) {
        qWarning() << "Can't reconnect previously connected service " << service_name;
        return false;
    }
    casted->dbus_interface_ = new typename Traits::dbus_import{casted};
    return true;
}

template<class Traits>
bool WaitAndConnectService(typename Traits::interface *service, Traits traits = {})
{
    return WaitForServiceRegistration(traits) && ConnectService(service, traits);
}


/// Exceptions
class Exception : public std::runtime_error
{
    using base = std::runtime_error;

public:
    using base::base;
    Exception(const QString &what) : base{what.toStdString()} {}
};

} // namespace hardbus

///////////////////////////////////////////////////////////////////////////////////
//////////////////////////////////// PRIVATE //////////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

namespace hardbus
{
/// Proxy string conversion
namespace internal
{
template<class T>
QString ToProxyString(const T &v)
{
    return ::hardbus::ProxyStringConverter<std::decay_t<T>>{}.ToString(v);
}

template<class T>
T FromProxyString(const QString &v)
{
    return ProxyStringConverter<std::decay_t<T>>{}.FromString(v);
}

struct FromProxyConverter
{
    FromProxyConverter() {}
    FromProxyConverter(QString v) : v_{std::move(v)} {}

    template<class T>
    operator T() const
    {
        return FromProxyString<T>(v_);
    }

private:
    QString v_;
};

struct ToProxyConverter
{
    ToProxyConverter() {}
    template<class T>
    ToProxyConverter(T v) : v_{ToProxyString(v)}
    {}

    operator QString() const { return v_; }

private:
    QString v_;
};
} // namespace internal

/// Macro helpers
namespace internal
{
// void overload
template<class T, class F>
auto CreaeteFromReturnValueImpl(const F &f)
    -> std::enable_if_t<std::is_same<void, std::result_of_t<F()>>::value, T>
{
    f();
    return {};
}

//non void overload
template<class T, class F>
auto CreaeteFromReturnValueImpl(const F &f)
    -> std::enable_if_t<!std::is_same<void, std::result_of_t<F()>>::value, T>
{
    return {f()};
}

//! Create an object \a T from return value of \a f.
//! Creates default contructed value if \a f returns void
template<class T, class F>
T CreaeteFromReturnValue(const F &f)
{
    return CreaeteFromReturnValueImpl<T>(f);
}

template<class RET, class ARG, class F, class T, class... Args>
RET ProxyCallHelper(F f, T *obj, Args &... str_args)
{
    auto wrapped = [&] { return (obj->*f)(ARG{str_args}...); };
    return CreaeteFromReturnValue<RET>(wrapped);
}

template<class... Args>
auto ProxyCallHelper1(Args &&... args)
{
    return ProxyCallHelper<ToProxyConverter, FromProxyConverter>(std::forward<Args>(args)...);
}

template<class F, class T, class... Args>
auto ProxyCallHelper2(F &&f, T *obj, Args &&... args)
{
    if (!obj)
        throw ::hardbus::Exception{"service not registered"};
    return ProxyCallHelper<FromProxyConverter, ToProxyConverter>(f,
                                                                 obj,
                                                                 std::forward<Args>(
                                                                     args)...);
}

template<class... Args>
QDBusReply<QString> CallFuncOverDBus(const QDBusAbstractInterface *interface,
                                     const QString &func_name,
                                     Args &&... str_args)
{
    QDBusReply<QString> res = const_cast<QDBusAbstractInterface &>(*interface)
                                  .call(func_name, std::forward<Args>(str_args)...);
    return res;
}

template<class PROXY, class S, class SF, class T, class TF>
bool MakeProxyConnector(S *source, SF source_signal, T *target, TF target_signal)
{
    QObject::connect(source, source_signal, [=](auto &&... args) {
        emit(target->*target_signal)(PROXY{std::forward<decltype(args)>(args)}...);
    });
    return true;
}

template<class... Args>
auto MakeProxyConnector1(Args &&... args)
{
    return MakeProxyConnector<FromProxyConverter>(std::forward<Args>(args)...);
}

template<class... Args>
auto MakeProxyConnector2(Args &&... args)
{
    return MakeProxyConnector<ToProxyConverter>(std::forward<Args>(args)...);
}

template<typename T, typename R, typename... Args>
R DeduceReturnType(R (T::*mf)(Args...));

template<typename T, typename R, typename... Args>
R DeduceReturnType(R (T::*mf)(Args...) const);

inline void ExportAdaptor(QObject *parent,
                          QDBusConnection connection,
                          QString object_path,
                          QString service_name)
{
    if (!connection.registerObject(object_path, parent)) {
        qWarning() << "Cannot register object at path" << object_path;
        throw ::hardbus::Exception{"Cannot register object at path" + object_path};
    }
    if (!connection.registerService(service_name)) {
        qWarning() << "Cannot register service" << service_name;
        throw ::hardbus::Exception{"Cannot register service" + object_path};
    }
}

template <class R, class F>
void ReturnValueOrVoid(const F &f, std::true_type)
{
    f();
}

template <class R, class F>
R ReturnValueOrVoid(const F &f, std::false_type)
{
    return f();
}

} // namespace internal

} // namespace hardbus

///////////////////////////////////////////////////////////////////////////////////
///////////////////////////////// PRIVATE MACRO ///////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

//////
#define HARDBUS_INTERNAL_DEFINE_TRAITS_(TAG, \
                                        INTERFACE, \
                                        BUS_SERVICE, \
                                        BUS_PATH, \
                                        BUS_INTERFACE, \
                                        BUS_TYPE) \
    struct TraitsFor##TAG \
    { \
        static constexpr const char *dbus_service_name = BUS_SERVICE; \
        static constexpr const char *dbus_service_path = BUS_PATH; \
        static constexpr const char *dbus_service_interface = BUS_INTERFACE; \
        static QDBusConnection connection_type() { return BUS_TYPE; }; \
        using interface = INTERFACE; \
        using dbus_export = class ExporAdaptorFor##TAG; \
        using dbus_import = class ImportAdaptorFor##TAG; \
        using access = class AccessFor##TAG; \
    };

//////

#define HARDBUS_INTERNAL_DEFINE_EXPORT_ADAPTOR_(TAG, BUS_INTERFACE, API) \
    class ExporAdaptorFor##TAG : public QDBusAbstractAdaptor \
    { \
        W_OBJECT(ExporAdaptorFor##TAG)  \
        W_CLASSINFO("D-Bus Interface", BUS_INTERFACE) \
\
        using Tag = TraitsFor##TAG; \
        using Interface = typename Tag::interface; \
        using Self = ExporAdaptorFor##TAG; \
        Interface *interface_; \
\
    public: \
        ExporAdaptorFor##TAG(Interface *interface) \
            : QDBusAbstractAdaptor(interface), interface_{interface} \
        { \
            Q_UNUSED(interface_); \
            setAutoRelaySignals(false); \
            ::hardbus::internal::ExportAdaptor(interface_, \
                                               Tag::connection_type(), \
                                               Tag::dbus_service_path, \
                                               Tag::dbus_service_name); \
        } \
    public: \
        API(HARDBUS_INTERNAL_EXPORT_FUNC_, HARDBUS_INTERNAL_EXPORT_SIGNAL_) \
    };

#define HARDBUS_INTERNAL_EXPORT_SIGNAL_(OUT, FUNC, ARGS) \
    OUT FUNC(HARDBUS_INTERNAL_TO_STRING_ARGS_ ARGS) HARDBUS_INTERNAL_W_SIGNAL(FUNC, ARGS) \
        private : bool connectror_for_##FUNC \
                  = ::hardbus::internal::MakeProxyConnector2(interface_, \
                                                             &Interface::FUNC, \
                                                             this, \
                                                             &Self::FUNC);

#define HARDBUS_INTERNAL_EXPORT_FUNC_(OUT, FUNC, ARGS, ...) \
    QString FUNC(HARDBUS_INTERNAL_TO_STRING_ARGS_ ARGS) \
    { \
        auto helper = [&](auto &&... args) { \
            return ::hardbus::internal::ProxyCallHelper1(&Interface::FUNC, \
                                                         interface_, \
                                                         HARDBUS_FWD(args)...); \
        }; \
        return helper(HARDBUS_INTERNAL_TO_ARGS_NAMES ARGS); \
    } \
    W_SLOT(FUNC)
//////

#define HARDBUS_INTERNAL_DEFINE_IMPORT_ADAPTOR_(TAG, API) \
    class ImportAdaptorFor##TAG : public QDBusAbstractInterface \
    { \
        W_OBJECT(ImportAdaptorFor##TAG) \
\
        using Tag = TraitsFor##TAG; \
        using Interface = Tag::interface; \
        using Self = ImportAdaptorFor##TAG; \
        Interface *interface_; \
\
    public: \
        explicit ImportAdaptorFor##TAG(Interface *interface) \
            : QDBusAbstractInterface(Tag::dbus_service_name, \
                                     Tag::dbus_service_path, \
                                     Tag::dbus_service_interface, \
                                     Tag::connection_type(), \
                                     interface), \
              interface_{interface} \
        { \
            Q_UNUSED(interface_); \
        } \
    public: \
        API(HARDBUS_INTERNAL_IMPORT_FUNC_, HARDBUS_INTERNAL_IMPORT_SIGNAL_) \
    };

#define HARDBUS_INTERNAL_IMPORT_FUNC_(OUT, FUNC, ARGS, ...) \
    QString FUNC(HARDBUS_INTERNAL_TO_STRING_ARGS_ ARGS) \
    { \
        auto helper = [&](auto &&... args) { \
            return ::hardbus::internal::CallFuncOverDBus(this, #FUNC, HARDBUS_FWD(args)...); \
        }; \
        return helper(HARDBUS_INTERNAL_TO_ARGS_NAMES ARGS); \
    }

#define HARDBUS_INTERNAL_IMPORT_SIGNAL_(OUT, FUNC, ARGS) \
    OUT FUNC(HARDBUS_INTERNAL_TO_STRING_ARGS_ ARGS) HARDBUS_INTERNAL_W_SIGNAL(FUNC, ARGS) \
\
        private : bool connectror_for_##FUNC \
                  = ::hardbus::internal::MakeProxyConnector1(this, \
                                                             &Self::FUNC, \
                                                             interface_, \
                                                             &Interface::FUNC);

//////
#define HARDBUS_INTERNAL_DEFINE_ACCESS_ADAPTOR_(TAG, API) \
    class AccessFor##TAG : public TraitsFor##TAG::interface \
    { \
        W_OBJECT(AccessFor##TAG) \
\
        using Tag = TraitsFor##TAG; \
        using DbusInterface = ImportAdaptorFor##TAG; \
        using Self = AccessFor##TAG; \
\
    public: \
        DbusInterface *dbus_interface_{nullptr}; \
        explicit AccessFor##TAG(QObject *parent = nullptr) { setParent(parent); } \
        API(HARDBUS_INTERNAL_ACCESS_FUNC_, HARDBUS_INTERNAL_ACCESS_SIGNAL_) \
    };

#define HARDBUS_INTERNAL_ACCESS_SIGNAL_(...) /*no impl*/

#define HARDBUS_INTERNAL_ACCESS_FUNC_(OUT, FUNC, ARGS, ...) \
    W_MACRO_REMOVEPAREN(OUT) FUNC(HARDBUS_INTERNAL_TO_ARGS_ ARGS) W_MACRO_REMOVEPAREN(__VA_ARGS__) override \
    { \
        using R = decltype(::hardbus::internal::DeduceReturnType(&Self::FUNC)); \
        auto helper = [&](auto &&... args) { \
            return ::hardbus::internal::ProxyCallHelper2(&DbusInterface::FUNC, \
                                                         dbus_interface_, \
                                                         HARDBUS_FWD(args)...); \
        }; \
        return ::hardbus::internal::ReturnValueOrVoid<R>([&] { return helper(HARDBUS_INTERNAL_TO_ARGS_NAMES ARGS); }, \
                                                         std::is_same<R, void>{}); \
    }


//////

#define HARDBUS_FWD(...) ::std::forward<decltype(__VA_ARGS__)>(__VA_ARGS__)

#define HARDBUS_INTERNAL_ARG_STRING_(...) QString
#define HARDBUS_INTERNAL_ARG_SAME_(...) __VA_ARGS__
#define HARDBUS_INTERNAL_ARG_VOID_(...) void

#define HARDBUS_INTERNAL_W_SIGNAL(FUNC, ARGS)  W_SIGNAL(FUNC BOOST_PP_COMMA_IF( BOOST_PP_NOT(TUPLE_IS_EMPTY ARGS) ) HARDBUS_INTERNAL_TO_ARGS_NAMES ARGS)

///////////////////////////////////////////////////////////////////////////////////
////////////////////////////////  LOW LEVEL MACRO /////////////////////////////////
///////////////////////////////////////////////////////////////////////////////////

#define HARDBUS_INTERNAL_ARG16_(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15
#define HARDBUS_INTERNAL_HAS_COMMA_(...) HARDBUS_INTERNAL_ARG16_(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define HARDBUS_INTERNAL_TRIGGER_PARENTHESIS_(...) ,
#define HARDBUS_INTERNAL_PASTE5_(_0, _1, _2, _3, _4) _0##_1##_2##_3##_4
#define HARDBUS_INTERNAL_IS_EMPTY_CASE_0001 ,
#define HARDBUS_INTERNAL_IS_EMPTY(_0, _1, _2, _3) HARDBUS_INTERNAL_HAS_COMMA_(HARDBUS_INTERNAL_PASTE5_(HARDBUS_INTERNAL_IS_EMPTY_CASE_, _0, _1, _2, _3))

#define TUPLE_IS_EMPTY(...) \
    HARDBUS_INTERNAL_IS_EMPTY(HARDBUS_INTERNAL_HAS_COMMA_(__VA_ARGS__), \
               HARDBUS_INTERNAL_HAS_COMMA_(HARDBUS_INTERNAL_TRIGGER_PARENTHESIS_ __VA_ARGS__), \
               HARDBUS_INTERNAL_HAS_COMMA_(__VA_ARGS__(/*empty*/)), \
               HARDBUS_INTERNAL_HAS_COMMA_(HARDBUS_INTERNAL_TRIGGER_PARENTHESIS_ __VA_ARGS__(/*empty*/)))

////
#define HARDBUS_INTERNAL_GEN_EMPTY_ARGS_(...) /*void*/

#define HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS_CB_(unused, data, idx, elem) BOOST_PP_COMMA_IF(idx) W_MACRO_REMOVEPAREN(elem) arg##idx

#define HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS_(seq) BOOST_PP_SEQ_FOR_EACH_I(HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS_CB_, ~, seq)

#define HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS__NAMES_CB(unused, data, idx, elem) BOOST_PP_COMMA_IF(idx) arg##idx

#define HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS__NAMES(seq) BOOST_PP_SEQ_FOR_EACH_I(HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS__NAMES_CB, ~, seq)

#define HARDBUS_INTERNAL_TO_ARGS_NAMES(...) \
    BOOST_PP_IF(TUPLE_IS_EMPTY(__VA_ARGS__), HARDBUS_INTERNAL_GEN_EMPTY_ARGS_, HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS__NAMES) \
    (BOOST_PP_TUPLE_TO_SEQ((__VA_ARGS__)))


#define HARDBUS_INTERNAL_TO_ARGS_(...) \
    BOOST_PP_IF(TUPLE_IS_EMPTY(__VA_ARGS__), HARDBUS_INTERNAL_GEN_EMPTY_ARGS_, HARDBUS_INTERNAL_GEN_NONEMPTY_ARGS_) \
    (BOOST_PP_TUPLE_TO_SEQ((__VA_ARGS__)))

/////////////////////////////////
#define HARDBUS_INTERNAL_GEN_NONEMPTY_STRING_ARGS_CB_(unused, data, idx, elem) \
    BOOST_PP_COMMA_IF(idx) QString arg##idx

#define HARDBUS_INTERNAL_GEN_NONEMPTY_STRING_ARGS_(seq) \
    BOOST_PP_SEQ_FOR_EACH_I(HARDBUS_INTERNAL_GEN_NONEMPTY_STRING_ARGS_CB_, ~, seq)

#define HARDBUS_INTERNAL_TO_STRING_ARGS_(...) \
    BOOST_PP_IF(TUPLE_IS_EMPTY(__VA_ARGS__), HARDBUS_INTERNAL_GEN_EMPTY_ARGS_, HARDBUS_INTERNAL_GEN_NONEMPTY_STRING_ARGS_) \
    (BOOST_PP_TUPLE_TO_SEQ((__VA_ARGS__)))


