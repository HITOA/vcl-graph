// A block of channels, sized by a template parameter (like Grog's Audio::AudioBlock).
template<typename T, uint32 N>
export struct Block {
    Array<T, N> channels;
}

export using Sample = float32;
export const uint32 Channels = 2;
