std::vector<float> getMasterWaveformSnapshot(std::size_t sampleCount)
{
    std::vector<float> result;
    masterWaveform.read([&](const float* samples,std::size_t count){
        result.assign(samples,samples+std::min(sampleCount,count));
    });
    return result;
}

std::size_t getMasterWaveformCapacity()
{
    return kMasterWaveformBufferSize;
}
