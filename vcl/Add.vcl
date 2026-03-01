@import "Audio.vcl";

@node_type NodeT;


in NodeT inputValue1;
in NodeT inputValue2;

out NodeT output;

template<typename T>
NodeT Add(NodeT value1, NodeT value2) {
    return value1 + value2;
}

template<>
Audio::AudioBuffer<1> Add<Audio::AudioBuffer<1>>(Audio::AudioBuffer<1> value1, Audio::AudioBuffer<1> value2) {
    Audio::AudioBuffer<2> result = value1;
    result.channels[0] += value2.channels[0];
    return result;
}

template<>
Audio::AudioBuffer<2> Add<Audio::AudioBuffer<2>>(Audio::AudioBuffer<2> value1, Audio::AudioBuffer<2> value2) {
    Audio::AudioBuffer<2> result = value1;
    result.channels[0] += value2.channels[0];
    result.channels[1] += value2.channels[1];
    return result;
}

[EntryPoint]
void Main() {
    output = Add<NodeT>(inputValue1, inputValue2);
}