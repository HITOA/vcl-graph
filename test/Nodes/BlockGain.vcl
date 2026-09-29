@import "Block.vcl";

// The sample type and channel count follow what's connected, as in Grog's library.
[AutoParameter]
using SampleType = float32;
[AutoParameter]
const uint32 ChannelCount = 1;

[Input("In")]
Block::Block<SampleType, ChannelCount> input;

[Output("Out")]
Block::Block<SampleType, ChannelCount> output;

[NodeProcess]
void Process() {
    for (uint32 i = 0; i < ChannelCount; ++i)
        output.channels[i] = input.channels[i] * 0.5;
}
