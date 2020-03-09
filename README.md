# hardbus
A header-only library adding user-defined types support to Qt D-Bus module.

D-Bus has support only for basic types like integer and string, so we are forced to build our D-Bus api with those basic types. In case we need to use our custom type we would need to serialize it to a string (e.g. JSON format) on the caller side and desirialize it back on the server side.

Typical example:
<TBD>

hardbus example:
```c++
//interface
class IFoo : public QObject
{
    Q_OBJECT
public:
    using QObject::QObject;

    virtual int Bar(CustomType v) = 0;
    virtual void Baz() const = 0;

signals:
    void Fuz(std::vector<int> arg1, int arg2);
};
```
```c++
//implementation
class Foo : public IFoo
{
    Q_OBJECT
public:
    Foo() {}

    int Bar(CustomType v) override;
    void Baz() const override;
};
```
```c++
//api description for hardbus (gmock style)
#define FOO_SERVICE_API(FUNC, SIG) \
    FUNC(int, Bar, (CustomType) ) \
    FUNC(void, Baz, (), (const)) \
    SIG(void, Fuz, (std::vector<int> , int) ) \
```
```c++
//generate definiton with all needed logic
HARDBUS_DEFINE_SERVICE(FooDefinition, /*name of the generated type*/
                       IFoo, /*interface*/
                       FOO_SERVICE_API, /*api*/
                       "com.hardus.Foo", /*dbus service name*/
                       "/com/hardus/Foo", /*dbus path*/
                       "com.hardus.FooInterface", /*dbus api*/
                       QDBusConnection::systemBus()) /*system or session bus*/
```
```c++
//generate implementation for our definition in some cpp file
HARDBUS_DEFINE_SERVICE_IMPL(FooDefinition)
```

To export service one can use
```c++
Foo foo; //interface implementation

// FooDefinition is our generated type
FooDefinition::RegisterService(&foo);
```
And to import remote service 
```c++
IFoo *remote_foo = FooDefinition::CreateServiceInterface();//create access class
//...
FooDefinition::WaitAndConnectService(remote_foo);//wait for server to export service 
```
Use access class as a normal interface class
```c++
remote_foo->Bar(CustomType{});
```

Hardbus converts all funtion arguments and return types to string, but does not defines how this conversion happens. It is left for the user.

Just add the following specialization in the hardbus namespace to define how the object should be serialized
```c++
namespace hardbus 
{
template<>
struct ProxyStringConverter<CustomType>
{
    QString ToString(const CustomType &v) { /*...*/}
    CustomType FromString(const QString &str){ /*...*/ }
};
} // namespace hardbus

```
