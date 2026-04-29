#pragma once

#include "RHI/RHI.hpp"

namespace ResourceUploader
{

enum class Lifetime : uint8
{
	Persistent,
	Scene,
};

void Init(usize persistentHeapSize, usize uploadHeapSize);
void Shutdown();

RHI::Resource Upload(Lifetime lifetime, const void* data, const RHI::ResourceDescription& description);

void Flush();
void Reset();

}
