#pragma once

// Sanctuary Choir - the inside of an AI singer: an RVC voice model (content encoder + generator) run
// through ONNX Runtime. Ported from VoxaVersa's engine (Source/neural/RvcModel.*), offline conversion only:
// no real-time stream, accents or neural pitch tracker (the choir gives every frame its pitch).

#include "NeuralRuntime.h"
#include "NeuralDsp.h"

#include <functional>

namespace sanctuary::ai
{
/** SOLA search when joining two inference segments (at least one period of an 80 Hz voice). */
inline constexpr double solaSearchSeconds = 0.0125;

/** Neural settings of a voice pack, read from the "model" object of its voice.json:

        "model": {
            "file": "model.onnx",            // RVC generator (required)
            "encoder": "contentvec-768",     // content encoder id -> <Encoders>/<id>.onnx
            "sampleRate": 40000,             // generator output rate
            "encoderFile": "encoder.onnx",   // optional: encoder shipped inside the pack (overrides "encoder")
            "hopSize": 400,                  // optional: generator samples per 10 ms frame (default sampleRate / 100)
            "speakerId": 0,                  // optional: speaker index for multi-speaker models
            "noiseScale": 0.66666            // optional: scale of the generator's "rnd" noise input
        }
*/
struct ModelConfig
{
    juce::File packDirectory, generatorFile, encoderFile;
    juce::String encoderId;
    int sampleRate = 0;
    int hopSize = 0;
    int speakerId = 0;
    float noiseScale = 0.66666f;
    bool allowAccelerator = false;   // the CPU: through DirectML VoxaVersa's singers came out with static

    /** Reads a pack folder's voice.json. `error` is set when it has no usable model. */
    static ModelConfig fromPack (const juce::File& packDirectory, juce::String& error);

    /** Encoder files to try, in order. `featureChannels` (from the generator) picks a default id. */
    juce::Array<juce::File> getEncoderCandidates (int featureChannels) const;
};

//==============================================================================
struct NeuralVoiceModel::Impl
{
    /** Scratch buffers for one caller of runFrames() (models are shared, workspaces are not). */
    struct Workspace
    {
        std::vector<float> phone, pitchf, noise, audio;
        std::vector<int64_t> pitch;
        std::vector<int32_t> pitch32;
        std::vector<uint16_t> encoderHalf, phoneHalf, pitchfHalf, noiseHalf;
        std::vector<uint8_t> mask;
        OutputTensor encoderOut, generatorOut;
    };

    /** `cancel` (optional) is checked between the loading steps. */
    bool initialise (const ModelConfig&, juce::String& error, const std::atomic<bool>* cancel = nullptr);

    /** The core of the pipeline. `audio16k` holds numFrames * 160 + dsp::encoderRightPad16k samples at
        16 kHz, `f0Frames` the target pitch of each of the numFrames 10 ms frames. The content encoder sees
        all frames, the generator only frames [generatorSkip, numFrames - generatorTail). On success ws.audio
        holds (numFrames - generatorSkip - generatorTail) * hopSize samples at the model rate, starting at
        frame generatorSkip. `firstFrame` is the absolute index of the first frame (keeps the noise consistent
        across overlapping segments); `offline` releases ONNX Runtime's arena memory after big runs. */
    bool runFrames (const float* audio16k, int numFrames, const float* f0Frames, int64_t firstFrame,
                    Workspace& ws, bool offline, juce::String& error, int generatorSkip = 0, int generatorTail = 0) const;

    bool convert (const juce::AudioBuffer<float>& input, double inputSampleRate,
                  const std::vector<float>& f0Hz, double f0FrameRate,
                  juce::AudioBuffer<float>& output, const std::atomic<bool>* cancel, juce::String& error,
                  const std::function<void (float)>& progress) const;

    //==============================================================================
    ModelConfig config;
    std::shared_ptr<OrtModelSession> encoder;     // shared between voices using the same encoder file
    std::unique_ptr<OrtModelSession> generator;

    int sampleRate = 0, hopSize = 0, featureChannels = 0, noiseChannels = 192;
    bool usesF0 = false;
    juce::String description;

    /** True when the generator or the encoder runs on an accelerator (GPU). */
    bool usesAccelerator() const noexcept;

    struct EncoderIO
    {
        enum class Layout { unknown, timeMajor, channelMajor };
        std::string input, mask, output;
        ONNXTensorElementDataType inputType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT;
        ONNXTensorElementDataType maskType = ONNX_TENSOR_ELEMENT_DATA_TYPE_BOOL;
        int inputRank = 3;
        Layout layout = Layout::unknown;
    } enc;

    struct GeneratorIO
    {
        std::string phone, lengths, pitch, pitchf, speaker, noise, output;
        ONNXTensorElementDataType phoneType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                  pitchfType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                  noiseType = ONNX_TENSOR_ELEMENT_DATA_TYPE_FLOAT,
                                  pitchType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                  lengthsType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64,
                                  speakerType = ONNX_TENSOR_ELEMENT_DATA_TYPE_INT64;
    } gen;

private:
    bool discoverGenerator (juce::String& error);
    bool discoverEncoder (juce::String& error);
    bool warmUp (juce::String& error, const std::atomic<bool>* cancel);
};

} // namespace sanctuary::ai
