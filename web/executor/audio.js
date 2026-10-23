// The page's sound: a ring of interleaved stereo floats at 48 kHz, filled
// from the Worker (main.js) and played out; silence when it runs dry. More
// than a quarter second queued: the oldest is dropped (a tab in the
// background, say), so sound stays close to the screen.
class ExecutorAudio extends AudioWorkletProcessor {
    constructor() {
        super();
        this.ring = new Float32Array(48000 * 2);  // a second
        this.read = 0;
        this.count = 0;                           // samples queued
        this.port.onmessage = (e) => this.push(e.data);
    }

    push(samples) {
        const ring = this.ring, size = ring.length;
        for (let i = 0; i < samples.length; i++)
            ring[(this.read + this.count + i) % size] = samples[i];
        this.count = Math.min(size, this.count + samples.length);
        const max = 48000 / 4 * 2;
        if (this.count > max) {
            this.read = (this.read + this.count - max) % size;
            this.count = max;
        }
    }

    process(inputs, outputs) {
        const [left, right] = outputs[0];
        const ring = this.ring, size = ring.length;
        for (let i = 0; i < left.length; i++) {
            if (this.count >= 2) {
                left[i] = ring[this.read];
                right[i] = ring[(this.read + 1) % size];
                this.read = (this.read + 2) % size;
                this.count -= 2;
            } else {
                left[i] = right[i] = 0;
            }
        }
        return true;
    }
}

registerProcessor('executor-audio', ExecutorAudio);
