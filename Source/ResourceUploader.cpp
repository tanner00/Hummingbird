#include "ResourceUploader.hpp"
#include "RenderContext.hpp"

using namespace RHI;

namespace ResourceUploader
{

static constexpr usize SingleSceneHeapSize = MB(256);

struct LinearHeap
{
	Heap Heap;
	usize Offset;
};

static Array<LinearHeap> SceneHeaps(&GlobalAllocator::Get());
static LinearHeap PersistentHeap;
static LinearHeap UploadHeap;

static GraphicsContext Graphics;

static Array<Resource> UploadBuffers(&GlobalAllocator::Get());

void Init(usize persistentHeapSize, usize uploadHeapSize)
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

void Shutdown()
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
}

Resource Upload(Lifetime lifetime, const void* data, const ResourceDescription& description)
{
	LinearHeap* heap = lifetime == Lifetime::Persistent ? &PersistentHeap
														: lifetime == Lifetime::Scene ? &SceneHeaps.Last() : nullptr;
	CHECK(heap);

	const usize resourceAlignment = GlobalDevice().GetResourceAlignment(description);
	heap->Offset = NextMultipleOf(heap->Offset, resourceAlignment);
	UploadHeap.Offset = NextMultipleOf(UploadHeap.Offset, resourceAlignment);

	const usize resourceSize = GlobalDevice().GetResourceSize(description);

	if (heap->Offset + resourceSize > heap->Heap.Size)
	{
		CHECK(lifetime == Lifetime::Scene);

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

	const usize resourceUploadSize = GlobalDevice().GetResourceStagingSize(description);
	CHECK(resourceUploadSize <= UploadHeap.Heap.Size);

	if (UploadHeap.Offset + resourceUploadSize > UploadHeap.Heap.Size)
	{
		Flush();
	}

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
		.Size = resourceUploadSize,
		.DebugName = "Upload Buffer"_view,
	});
	UploadBuffers.Add(uploadBuffer);

	const Resource resource = GlobalDevice().Create(PlaceResource(description, heap->Heap, heap->Offset));

	GlobalDevice().Write(&uploadBuffer, resource, data);

	Graphics.Copy(resource, uploadBuffer);

	heap->Offset += resourceSize;
	UploadHeap.Offset += resourceUploadSize;

	return resource;
}

void Flush()
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

void Reset()
{
	Flush();

	while (SceneHeaps.GetCount() > 1)
	{
		GlobalDevice().Destroy(&SceneHeaps.Last().Heap);
		SceneHeaps.Remove(SceneHeaps.GetCount() - 1);
	}
	SceneHeaps.First().Offset = 0;
}

}
