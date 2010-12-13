#include <QStringList>

#include <pulse/context.h>
#include <pulse/glib-mainloop.h>
#include <pulse/introspect.h>
#include <pulse/operation.h>
#include <pulse/subscribe.h>

#include "pacontroller.h"


PAController::PAController(QObject *parent) :
    QObject(parent),
    inputToBeMoved(PA_INVALID_INDEX),
    inputModel(new SinkInputModel(this)),
    sinkModel(new SinkModel(this))
{
    /* initialize PulseAudio data structures */
    mainloop = pa_glib_mainloop_new(NULL);
    context = pa_context_new(pa_glib_mainloop_get_api(mainloop), "conductor");
}

PAController::~PAController()
{
    /* unload all modules loaded by us */
    pa_operation *op;
    foreach (uint32_t idx, loadedModules) {
        if ((op = pa_context_unload_module(context, idx, NULL, NULL)))
            pa_operation_unref(op);
    }

    /* disconnect and release resources */
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
        emit self->error(tr("failed to connect to PulseAudio daemon"));
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

bool PAController::createTunnel(const QByteArray &server)
{
    if (server.trimmed().isEmpty())
        return false;

    QByteArray args = server.trimmed().prepend("server=");
    pa_operation *op;

    if (!(op = pa_context_load_module(context, "module-tunnel-sink", args, PAController::tunnelCallback, this)))
        return false;

    pa_operation_unref(op);
    return true;
}

void PAController::tunnelCallback(pa_context *, uint32_t idx, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);

    if (idx != PA_INVALID_INDEX)
        self->loadedModules.prepend(idx);
}

void PAController::moveSinkInput(const SinkInput &input, const QList<QByteArray> &speakers)
{
    if (speakers.isEmpty())
        return;

    inputToBeMoved = input.index();

    if (speakers.count() == 1) {
        QByteArray sink = speakers.first().trimmed().prepend("tunnel.");
        pa_operation *op;

        if (!(op = pa_context_move_sink_input_by_name(context, inputToBeMoved, sink, PAController::moveCallback, this)))
            emit warning("pa_context_move_sink_input_by_name() failed");
        else
            pa_operation_unref(op);
    } else {
        combineTunnels(speakers, "combined", 10, "src-sinc-best-quality");
    }
}

void PAController::moveCallback(pa_context *, int success, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);

    if (!success)
        emit self->warning(tr("failed to move stream #") + self->inputToBeMoved);

    self->inputToBeMoved = PA_INVALID_INDEX;
}

void PAController::combineTunnels(const QList<QByteArray> &addresses, const QString &name, int adjustTime, const QString &resampleMethod)
{
    /* build the list of arguments to module-combine */
    QStringList arglist;

    QString slaves;
    foreach (QByteArray addr, addresses)
        slaves += addr.trimmed().prepend("tunnel.") + ',';
    slaves.chop(1);
    arglist << slaves.prepend("slaves=");

    if (!name.trimmed().isEmpty())
        arglist << name.trimmed().prepend("sink_name=");
    if (adjustTime >= 0)
        arglist << QString::number(adjustTime).prepend("adjust_time=");
    if (!resampleMethod.trimmed().isEmpty())
        arglist << resampleMethod.trimmed().prepend("resample_method=");

    QByteArray args = arglist.join(" ").toLocal8Bit();
    pa_operation *op;

    if (!(op = pa_context_load_module(context, "module-combine", args, PAController::combineCallback, this)))
        emit warning("pa_context_load_module() failed");
    else
        pa_operation_unref(op);
}

void PAController::combineCallback(pa_context *c, uint32_t idx, void *userdata)
{
    PAController *self = static_cast<PAController*>(userdata);

    if (idx == PA_INVALID_INDEX)
        return;

    self->loadedModules.prepend(idx);

    if (self->inputToBeMoved != PA_INVALID_INDEX) {
        pa_operation *op;

        if (!(op = pa_context_move_sink_input_by_name(c, self->inputToBeMoved, "combined", PAController::moveCallback, self)))
            emit self->warning("pa_context_move_sink_input_by_name() failed");
        else
            pa_operation_unref(op);
    }
}
