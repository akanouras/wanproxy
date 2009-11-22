#include <common/buffer.h>

#include <event/action.h>
#include <event/callback.h>
#include <event/event_system.h>

#include <io/pipe.h>
#include <io/pipe_simple.h>

/*
 * PipeSimple is a pipe which passes data through a processing function and
 * which provides no discipline -- it will always immediately signal that it is
 * ready to process more data.
 *
 * TODO: Asynchronous processing function.
 *
 * XXX Should flag error and ensure all subsequent input() and output() fail.
 */

PipeSimple::PipeSimple(const LogHandle& log)
: log_(log),
  input_buffer_(),
  input_eos_(false),
  output_action_(NULL),
  output_callback_(NULL)
{
}

PipeSimple::~PipeSimple()
{
	ASSERT(output_action_ == NULL);
	ASSERT(output_callback_ == NULL);
}

Action *
PipeSimple::input(Buffer *buf, EventCallback *cb)
{
	if (buf->empty()) {
		input_eos_ = true;
	}

	if (output_callback_ != NULL) {
		ASSERT(input_buffer_.empty());
		ASSERT(output_action_ == NULL);

		Buffer tmp;
		if (!process(&tmp, buf)) {
			output_callback_->event(Event(Event::Error, 0));
			output_action_ = EventSystem::instance()->schedule(output_callback_);
			output_callback_ = NULL;

			cb->event(Event(Event::Error, 0));
			return (EventSystem::instance()->schedule(cb));
		}
		ASSERT(buf->empty());

		if (!tmp.empty() || input_eos_) {
			if (input_eos_ && tmp.empty()) {
				output_callback_->event(Event(Event::EOS, 0));
			} else {
				output_callback_->event(Event(Event::Done, 0, tmp));
			}
			output_action_ = EventSystem::instance()->schedule(output_callback_);
			output_callback_ = NULL;
		}
	} else {
		if (!buf->empty()) {
			input_buffer_.append(buf);
			buf->clear();
		}
	}

	cb->event(Event(Event::Done, 0));
	return (EventSystem::instance()->schedule(cb));
}

Action *
PipeSimple::output(EventCallback *cb)
{
	ASSERT(output_action_ == NULL);
	ASSERT(output_callback_ == NULL);

	if (!input_buffer_.empty() || input_eos_) {
		Buffer tmp;
		if (!process(&tmp, &input_buffer_)) {
			cb->event(Event(Event::Error, 0));
			return (EventSystem::instance()->schedule(cb));
		}
		ASSERT(input_buffer_.empty());

		if (!tmp.empty() || input_eos_) {
			if (input_eos_ && tmp.empty()) {
				cb->event(Event(Event::EOS, 0));
			} else {
				ASSERT(!tmp.empty());
				cb->event(Event(Event::Done, 0, tmp));
			}

			return (EventSystem::instance()->schedule(cb));
		}
	}

	output_callback_ = cb;

	return (cancellation(this, &PipeSimple::output_cancel));
}

void
PipeSimple::output_cancel(void)
{
	if (output_action_ != NULL) {
		ASSERT(output_callback_ == NULL);

		output_action_->cancel();
		output_action_ = NULL;
	}

	if (output_callback_ != NULL) {
		delete output_callback_;
		output_callback_ = NULL;
	}
}

void
PipeSimple::output_spontaneous(void)
{
	if (output_callback_ == NULL)
		return;

	Buffer tmp;
	if (!process(&tmp, &input_buffer_)) {
		output_callback_->event(Event(Event::Error, 0));
		output_action_ = EventSystem::instance()->schedule(output_callback_);
		output_callback_ = NULL;

		return;
	}
	ASSERT(input_buffer_.empty());

	/*
	 * XXX
	 * Would prefer for this to never happen!
	 */
	if (tmp.empty() && !input_eos_) {
		DEBUG(log_) << "Spontaneous output generated no output despite EOS being unset.";
		return;
	}

	if (tmp.empty()) {
		output_callback_->event(Event(Event::EOS, 0));
	} else {
		output_callback_->event(Event(Event::Done, 0, tmp));
	}
	output_action_ = EventSystem::instance()->schedule(output_callback_);
	output_callback_ = NULL;
}