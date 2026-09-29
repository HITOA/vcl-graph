@import "Block.vcl";

// Written with the library's alias and constant, as Grog's Audio Input is.
[Output("Out")]
Block::Block<Block::Sample, Block::Channels> output;

[NodeProcess]
void Process() {
    output.channels[0] = 1.0;
    output.channels[1] = 2.0;
}
