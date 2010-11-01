#include <QStringList>

#include <pulse/context.h>
#include <pulse/glib-mainloop.h>
#include <pulse/introspect.h>
#include <pulse/operation.h>
#include <pulse/subscribe.h>

#include "pacontroller.h"


PAController::PAController(QObject *parent) :
    QObject(parent),
    inputModel(new SinkInputModel(this)),
    sinkModel(new SinkModel(this))
{
    /* initialize PulseAudio data structures */
    mainloop = pa_glib_mainloop_new(NULL);
    context = pa_context_new(pa_glib_mainloop_get_api(mainloop), "conductor");
}

PAController::~PAController()
{
    pa_context_disconnect(context);
    pa_context_unref(context);
    pa_glib_mainloop_free(mainloop);
}

void PAController::connectToDaemon(const QByteArray &server)
{
    pa_context_set_state_callback(context, PAController::stateCallback, this);

    /* connect to the daemon */
    if (pa_context_connect(context, server.isEmpty() ? NULL : server.constData(), PA_CONTEXT_NOFAIL, NULL) < 0)
        emit error(tr("pa_context_connect() failed"));
}

void PAController::stateCallback(pa_context *c, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);

    switch (pa_context_get_state(c)) {

    case PA_CONTEXT_READY: {
        emit self->connected(pa_context_get_server(c), pa_context_is_local(c));

        /* subscribe to client, sink and sink input events */
        pa_context_set_subscribe_callback(c, PAController::subscribeCallback, self);
        pa_operation *op = pa_context_subscribe(c, (pa_subscription_mask_t)
                                                (PA_SUBSCRIPTION_MASK_CLIENT |
                                                 PA_SUBSCRIPTION_MASK_SINK |
                                                 PA_SUBSCRIPTION_MASK_SINK_INPUT),
                                                NULL, NULL);
        if (op)
            pa_operation_unref(op);
        else
            emit self->warning(tr("pa_context_subscribe() failed"));

        /* initialize table models */
        if (!self->inputModel->populate(c))
            emit self->warning(tr("failed to populate SinkInputModel"));
        if (!self->sinkModel->populate(c))
            emit self->warning(tr("failed to populate SinkModel"));
        break;
    }

    case PA_CONTEXT_FAILED:
        emit self->error(tr("PA context failure"));
        break;

    default:
        break;
    }
}

void PAController::subscribeCallback(pa_context *c, pa_subscription_event_type_t t, uint32_t idx, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);

    /* dispatch the event to the appropriate model for further processing */
    switch (t & PA_SUBSCRIPTION_EVENT_FACILITY_MASK) {

    case PA_SUBSCRIPTION_EVENT_CLIENT:
        switch (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) {
        case PA_SUBSCRIPTION_EVENT_NEW:
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            self->inputModel->updateClient(c, idx);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            self->inputModel->removeClient(idx);
            break;
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SINK:
        switch (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) {
        case PA_SUBSCRIPTION_EVENT_NEW:
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            self->sinkModel->updateSink(c, idx);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            self->sinkModel->removeSink(idx);
            break;
        }
        break;

    case PA_SUBSCRIPTION_EVENT_SINK_INPUT:
        switch (t & PA_SUBSCRIPTION_EVENT_TYPE_MASK) {
        case PA_SUBSCRIPTION_EVENT_NEW:
        case PA_SUBSCRIPTION_EVENT_CHANGE:
            self->inputModel->updateSinkInput(c, idx);
            break;
        case PA_SUBSCRIPTION_EVENT_REMOVE:
            self->inputModel->removeSinkInput(idx);
            break;
        }
        break;
    }
}

bool PAController::combineSinks(const QList<uint32_t> &sinks, const QString &name)
{
    if (sinks.size() < 2)
        return false;

    /* build the list of arguments to module-combine */
    QStringList arglist;
    QString slaves;
    foreach (uint32_t s, sinks)
        slaves += s + ',';
    slaves.chop(1);
    arglist << slaves.prepend("slaves=");
    if (!name.isEmpty())
        arglist << name.trimmed().prepend("sink_name=");
    arglist << "adjust_time=5";
    arglist << "resample_method=src-sinc-best-quality";

    const char *args = arglist.join(" ").toLocal8Bit().constData();
    pa_operation *op;

    if (!(op = pa_context_load_module(context, "module-combine", args, PAController::combineCallback, this)))
        return false;

    pa_operation_unref(op);
    return true;
}

void PAController::combineCallback(pa_context *, uint32_t, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);
    Q_UNUSED(self);
}

bool PAController::createTunnel(const QByteArray &server)
{
    if (server.isEmpty())
        return false;

    const char *args = server.trimmed().prepend("server=").constData();
    pa_operation *op;

    if (!(op = pa_context_load_module(context, "module-tunnel-sink", args, PAController::tunnelCallback, this)))
        return false;

    pa_operation_unref(op);
    return true;
}

void PAController::tunnelCallback(pa_context *, uint32_t, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);
    Q_UNUSED(self);
}
