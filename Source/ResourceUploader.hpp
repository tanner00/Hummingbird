#pragma once

#include "RHI/RHI.hpp"

struct LinearHeap
{
	RHI::Heap Heap;
	usize Offset;
};

enum class ResourceLifetime : uint8
{
	Persistent,
	Scene,
};

class ResourceUploader : public NoCopy
{
public:
	void Init(usize persistentHeapSize, usize uploadHeapSize);
	void Shutdown();

	static ResourceUploader& Get()
	{
		static ResourceUploader instance;
		return instance;
	}

	RHI::Resource Upload(ResourceLifetime lifetime, const void* data, const RHI::ResourceDescription& description);

	void Flush();
	void Reset();

private:
	ResourceUploader()
		: SceneHeaps(&GlobalAllocator::Get())
		, PersistentHeap()
		, UploadHeap()
		, UploadBuffers(&GlobalAllocator::Get())
	{
	}

	Array<LinearHeap> SceneHeaps;
	LinearHeap PersistentHeap;
	LinearHeap UploadHeap;

	RHI::GraphicsContext Graphics;

	Array<RHI::Resource> UploadBuffers;
};
