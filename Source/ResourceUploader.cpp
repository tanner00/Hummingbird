#include "ResourceUploader.hpp"
#include "RenderContext.hpp"

using namespace RHI;

static constexpr usize SingleSceneHeapSize = MB(256);

void ResourceUploader::Init(usize persistentHeapSize, usize uploadHeapSize)
{
	SceneHeaps.Add(LinearHeap
	{
		.Heap = GlobalDevice().Create(
		{
			.Type = HeapType::Default,
			.Size = SingleSceneHeapSize,
		}),
		.Offset = 0,
	});
	PersistentHeap = LinearHeap
	{
		.Heap = GlobalDevice().Create(
		{
			.Type = HeapType::Default,
			.Size = persistentHeapSize,
		}),
		.Offset = 0,
	};
	UploadHeap = LinearHeap
	{
		.Heap = GlobalDevice().Create(
		{
			.Type = HeapType::Upload,
			.Size = uploadHeapSize,
		}),
		.Offset = 0,
	};

	Graphics = GlobalDevice().Create(GraphicsContextDescription {});
	Graphics.Begin();
}

void ResourceUploader::Shutdown()
{
	for (LinearHeap& heap : SceneHeaps)
	{
		GlobalDevice().Destroy(&heap.Heap);
	}
	GlobalDevice().Destroy(&PersistentHeap.Heap);
	GlobalDevice().Destroy(&UploadHeap.Heap);

	GlobalDevice().Destroy(&Graphics);

	for (Resource& uploadBuffer : UploadBuffers)
	{
		GlobalDevice().Destroy(&uploadBuffer);
	}

	this->~ResourceUploader();
}

Resource ResourceUploader::Upload(ResourceLifetime lifetime, const void* data, const ResourceDescription& description)
{
	LinearHeap* heap = lifetime == ResourceLifetime::Persistent ? &PersistentHeap
																: (lifetime == ResourceLifetime::Scene ? &SceneHeaps.Last() : nullptr);
	CHECK(heap);

	heap->Offset = NextMultipleOf(heap->Offset, GlobalDevice().GetResourceAlignment(description));
	UploadHeap.Offset = NextMultipleOf(UploadHeap.Offset, GlobalDevice().GetResourceAlignment(description));

	const usize resourceSize = GlobalDevice().GetResourceSize(description);
	CHECK(resourceSize <= UploadHeap.Heap.Size);

	if (heap->Offset + resourceSize > heap->Heap.Size)
	{
		CHECK(lifetime == ResourceLifetime::Scene);

		SceneHeaps.Add(LinearHeap
		{
			.Heap = GlobalDevice().Create(
			{
				.Type = HeapType::Default,
				.Size = SingleSceneHeapSize,
			}),
			.Offset = 0,
		});
		heap = &SceneHeaps.Last();
	}

	if (UploadHeap.Offset + resourceSize > UploadHeap.Heap.Size)
	{
		Flush();
	}

	const usize uploadBufferSize = description.Type == ResourceType::Buffer ? description.Size : resourceSize;
	const Resource uploadBuffer = GlobalDevice().Create(
	{
		.Type = ResourceType::Buffer,
		.Flags = ResourceFlags::Upload,
		.InitialLayout = BarrierLayout::Undefined,
		.Allocation = ResourceAllocation
		{
			.Heap = UploadHeap.Heap,
			.Offset = UploadHeap.Offset,
		},
		.Size = uploadBufferSize,
		.Name = "Upload Buffer"_view,
	});
	UploadBuffers.Add(uploadBuffer);

	const Resource resource = GlobalDevice().Create(PlaceResource(description, heap->Heap, heap->Offset));

	GlobalDevice().Write(&uploadBuffer, resource, data);

	Graphics.Copy(resource, uploadBuffer);

	heap->Offset += resourceSize;
	UploadHeap.Offset += resourceSize;

	return resource;
}

void ResourceUploader::Flush()
{
	if (UploadHeap.Offset == 0)
	{
		return;
	}

	Graphics.End();
	GlobalDevice().Submit(Graphics);
	GlobalDevice().WaitForIdle();

	for (Resource& uploadBuffer : UploadBuffers)
	{
		GlobalDevice().Destroy(&uploadBuffer);
	}
	UploadBuffers.Clear();

	UploadHeap.Offset = 0;

	Graphics.Begin();
}

void ResourceUploader::Reset()
{
	Flush();

	while (SceneHeaps.GetLength() > 1)
	{
		GlobalDevice().Destroy(&SceneHeaps.Last().Heap);
		SceneHeaps.Remove(SceneHeaps.GetLength() - 1);
	}
	SceneHeaps.First().Offset = 0;
}
