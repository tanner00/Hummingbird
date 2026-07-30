#include "ResourceUploader.hpp"
#include "RenderContext.hpp"

using namespace RHI;

namespace ResourceUploader
{

static constexpr usize SingleHeapSize = MB(256);

struct LinearHeap
{
	Heap Heap;
	usize Offset;
};

static Array<LinearHeap> SceneHeaps(&GlobalAllocator::Get());
static Array<LinearHeap> PersistentHeaps(&GlobalAllocator::Get());
static LinearHeap UploadHeap;

static GraphicsContext Graphics;

static Array<Resource> UploadBuffers(&GlobalAllocator::Get());

void Init()
{
	SceneHeaps.Add(LinearHeap
	{
		.Heap = GlobalDevice().Create(HeapDescription
		{
			.Type = HeapType::Default,
			.Size = SingleHeapSize,
		}),
		.Offset = 0,
	});
	PersistentHeaps.Add(LinearHeap
	{
		.Heap = GlobalDevice().Create(HeapDescription
		{
			.Type = HeapType::Default,
			.Size = SingleHeapSize,
		}),
		.Offset = 0,
	});
	UploadHeap = LinearHeap
	{
		.Heap = GlobalDevice().Create(HeapDescription
		{
			.Type = HeapType::Upload,
			.Size = SingleHeapSize,
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
	for (LinearHeap& heap : PersistentHeaps)
	{
		GlobalDevice().Destroy(&heap.Heap);
	}
	GlobalDevice().Destroy(&UploadHeap.Heap);

	GlobalDevice().Destroy(&Graphics);

	for (Resource& uploadBuffer : UploadBuffers)
	{
		GlobalDevice().Destroy(&uploadBuffer);
	}
}

Resource Upload(Lifetime lifetime, const void* data, const ResourceDescription& description)
{
	Array<LinearHeap>* heaps = lifetime == Lifetime::Persistent ? &PersistentHeaps : &SceneHeaps;
	LinearHeap* heap = &heaps->Last();

	const usize resourceAlignment = GlobalDevice().GetResourceAlignment(description);
	heap->Offset = NextMultipleOf(heap->Offset, resourceAlignment);
	UploadHeap.Offset = NextMultipleOf(UploadHeap.Offset, resourceAlignment);

	const usize resourceSize = GlobalDevice().GetResourceSize(description);

	if (heap->Offset + resourceSize > heap->Heap.Size)
	{
		heaps->Add(LinearHeap
		{
			.Heap = GlobalDevice().Create(HeapDescription
			{
				.Type = HeapType::Default,
				.Size = SingleHeapSize,
			}),
			.Offset = 0,
		});
		heap = &heaps->Last();
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
