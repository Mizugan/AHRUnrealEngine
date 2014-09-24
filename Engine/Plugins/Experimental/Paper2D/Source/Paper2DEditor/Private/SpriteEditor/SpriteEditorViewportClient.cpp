// Copyright 1998-2014 Epic Games, Inc. All Rights Reserved.

#include "Paper2DEditorPrivatePCH.h"
#include "SpriteEditorViewportClient.h"
#include "SceneViewport.h"

#include "PreviewScene.h"
#include "ScopedTransaction.h"
#include "Runtime/Engine/Public/ComponentReregisterContext.h"

#define LOCTEXT_NAMESPACE "SpriteEditor"

//////////////////////////////////////////////////////////////////////////
// FSelectionTypes

const FName FSelectionTypes::Vertex(TEXT("Vertex"));
const FName FSelectionTypes::Edge(TEXT("Edge"));
const FName FSelectionTypes::Pivot(TEXT("Pivot"));
const FName FSelectionTypes::Socket(TEXT("Socket"));
const FName FSelectionTypes::SourceRegion(TEXT("SourceRegion"));

//////////////////////////////////////////////////////////////////////////
// HSpriteSelectableObjectHitProxy

struct HSpriteSelectableObjectHitProxy : public HHitProxy
{
	DECLARE_HIT_PROXY( PAPER2DEDITOR_API );

	TSharedPtr<FSelectedItem> Data;

	HSpriteSelectableObjectHitProxy(TSharedPtr<FSelectedItem> InData)
		: HHitProxy(HPP_UI)
		, Data(InData)
	{
	}
};
IMPLEMENT_HIT_PROXY(HSpriteSelectableObjectHitProxy, HHitProxy);

//////////////////////////////////////////////////////////////////////////
// FSpriteEditorViewportClient

FSpriteEditorViewportClient::FSpriteEditorViewportClient(TWeakPtr<FSpriteEditor> InSpriteEditor, TWeakPtr<class SSpriteEditorViewport> InSpriteEditorViewportPtr)
	: CurrentMode(ESpriteEditorMode::ViewMode)
	, SpriteEditorPtr(InSpriteEditor)
	, SpriteEditorViewportPtr(InSpriteEditorViewportPtr)
{
	check(SpriteEditorPtr.IsValid() && SpriteEditorViewportPtr.IsValid());

	PreviewScene = &OwnedPreviewScene;

	SetRealtime(true);

	WidgetMode = FWidget::WM_Translate;
	bManipulating = false;
	bManipulationDirtiedSomething = false;
	ScopedTransaction = NULL;

	bShowSourceTexture = false;
	bShowSockets = true;
	bShowNormals = true;
	bShowPivot = true;

	bDeferZoomToSprite = true;

	EngineShowFlags.DisableAdvancedFeatures();
	EngineShowFlags.CompositeEditorPrimitives = true;

	// Create a render component for the sprite being edited
	{
		RenderSpriteComponent = NewObject<UPaperSpriteComponent>();
		UPaperSprite* Sprite = GetSpriteBeingEdited();
		RenderSpriteComponent->SetSprite(Sprite);

		PreviewScene->AddComponent(RenderSpriteComponent, FTransform::Identity);
	}

	// Create a sprite and render component for the source texture view
	{
		UPaperSprite* DummySprite = NewObject<UPaperSprite>();
		DummySprite->SpriteCollisionDomain = ESpriteCollisionMode::None;
		DummySprite->PivotMode = ESpritePivotMode::Bottom_Left;

		SourceTextureViewComponent = NewObject<UPaperSpriteComponent>();
		SourceTextureViewComponent->SetSprite(DummySprite);
		UpdateSourceTextureSpriteFromSprite(GetSpriteBeingEdited());

		// Nudge the source texture view back a bit so it doesn't occlude sprites
		const FTransform Transform(-1.0f * PaperAxisZ);
		SourceTextureViewComponent->bVisible = false;
		PreviewScene->AddComponent(SourceTextureViewComponent, Transform);
	}
}

void FSpriteEditorViewportClient::UpdateSourceTextureSpriteFromSprite(UPaperSprite* SourceSprite)
{
	UPaperSprite* TargetSprite = SourceTextureViewComponent->GetSprite();
	check(TargetSprite);

	if (SourceSprite != NULL)
	{
		if ((SourceSprite->GetSourceTexture() != TargetSprite->GetSourceTexture()) || (TargetSprite->PixelsPerUnrealUnit != SourceSprite->PixelsPerUnrealUnit))
		{
			FComponentReregisterContext ReregisterSprite(SourceTextureViewComponent);
			TargetSprite->InitializeSprite(SourceSprite->SourceTexture, SourceSprite->PixelsPerUnrealUnit);
		}
	}
	else
	{
		// No source sprite, so don't draw the target either
		TargetSprite->SourceTexture = NULL;
	}
}

FVector2D FSpriteEditorViewportClient::TextureSpaceToScreenSpace(const FSceneView& View, const FVector2D& SourcePoint) const
{
	const FVector WorldSpacePoint = TextureSpaceToWorldSpace(SourcePoint);

	FVector2D PixelLocation;
	View.WorldToPixel(WorldSpacePoint, /*out*/ PixelLocation);
	return PixelLocation;
}

FVector FSpriteEditorViewportClient::TextureSpaceToWorldSpace(const FVector2D& SourcePoint) const
{
	UPaperSprite* Sprite = RenderSpriteComponent->GetSprite();
	return Sprite->ConvertTextureSpaceToWorldSpace(SourcePoint);
}

FVector2D FSpriteEditorViewportClient::SourceTextureSpaceToScreenSpace(const FSceneView& View, const FVector2D& SourcePoint) const
{
	const FVector WorldSpacePoint = SourceTextureSpaceToWorldSpace(SourcePoint);

	FVector2D PixelLocation;
	View.WorldToPixel(WorldSpacePoint, /*out*/ PixelLocation);
	return PixelLocation;
}

FVector FSpriteEditorViewportClient::SourceTextureSpaceToWorldSpace(const FVector2D& SourcePoint) const
{
	UPaperSprite* Sprite = SourceTextureViewComponent->GetSprite();
	return Sprite->ConvertTextureSpaceToWorldSpace(SourcePoint);
}

void FSpriteEditorViewportClient::DrawTriangleList(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, const TArray<FVector2D>& Triangles)
{
	// Draw the collision data (non-interactive)
	const FLinearColor BakedCollisionRenderColor(1.0f, 1.0f, 0.0f, 0.5f);
	float BakedCollisionVertexSize = 3.0f;

	// Draw the vertices
	for (int32 VertexIndex = 0; VertexIndex < Triangles.Num(); ++VertexIndex)
	{
		const FVector2D VertexPos(TextureSpaceToScreenSpace(View, Triangles[VertexIndex]));
		Canvas.DrawTile(VertexPos.X - BakedCollisionVertexSize*0.5f, VertexPos.Y - BakedCollisionVertexSize*0.5f, BakedCollisionVertexSize, BakedCollisionVertexSize, 0.f, 0.f, 1.f, 1.f, BakedCollisionRenderColor, GWhiteTexture);
	}

	// Draw the lines
	const FLinearColor BakedCollisionLineRenderColor(1.0f, 1.0f, 0.0f, 0.25f);
	for (int32 TriangleIndex = 0; TriangleIndex < Triangles.Num() / 3; ++TriangleIndex)
	{
		for (int32 Offset = 0; Offset < 3; ++Offset)
		{
			const int32 VertexIndex = (TriangleIndex * 3) + Offset;
			const int32 NextVertexIndex = (TriangleIndex * 3) + ((Offset + 1) % 3);

			const FVector2D VertexPos(TextureSpaceToScreenSpace(View, Triangles[VertexIndex]));
			const FVector2D NextVertexPos(TextureSpaceToScreenSpace(View, Triangles[NextVertexIndex]));

			FCanvasLineItem LineItem(VertexPos, NextVertexPos);
			LineItem.SetColor(BakedCollisionLineRenderColor);
			Canvas.DrawItem(LineItem);
		}
	}
}

void FSpriteEditorViewportClient::DrawSourceRegion(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, const FLinearColor& GeometryVertexColor, bool bIsRenderGeometry)
{
	const bool bIsHitTesting = Canvas.IsHitTesting();
	UPaperSprite* Sprite = GetSpriteBeingEdited();

    const float CornerCollisionVertexSize = 8.0f;
	const float EdgeCollisionVertexSize = 6.0f;

	const float NormalLength = 15.0f;
	const FLinearColor GeometryLineColor(GeometryVertexColor.R, GeometryVertexColor.G, GeometryVertexColor.B, 0.5f * GeometryVertexColor.A);
	const FLinearColor GeometryNormalColor(0.0f, 1.0f, 0.0f, 0.5f);
    
    const bool bDrawEdgeHitProxies = true;
    const bool bDrawCornerHitProxies = true;

	FVector2D BoundsVertices[4];
	BoundsVertices[0] = SourceTextureSpaceToScreenSpace(View, Sprite->SourceUV);
	BoundsVertices[1] = SourceTextureSpaceToScreenSpace(View, Sprite->SourceUV + FVector2D(Sprite->SourceDimension.X, 0));
	BoundsVertices[2] = SourceTextureSpaceToScreenSpace(View, Sprite->SourceUV + FVector2D(Sprite->SourceDimension.X, Sprite->SourceDimension.Y));
	BoundsVertices[3] = SourceTextureSpaceToScreenSpace(View, Sprite->SourceUV + FVector2D(0, Sprite->SourceDimension.Y));
	for (int32 VertexIndex = 0; VertexIndex < 4; ++VertexIndex)
	{
		const int32 NextVertexIndex = (VertexIndex + 1) % 4;

		FCanvasLineItem LineItem(BoundsVertices[VertexIndex], BoundsVertices[NextVertexIndex]);
		LineItem.SetColor(GeometryVertexColor);
		Canvas.DrawItem(LineItem);

		FVector2D MidPoint = (BoundsVertices[VertexIndex] + BoundsVertices[NextVertexIndex]) * 0.5f;
        FVector2D CornerPoint = BoundsVertices[VertexIndex];

        // Add edge hit proxy
        if (bDrawEdgeHitProxies)
        {
            if (bIsHitTesting)
            {
                TSharedPtr<FSpriteSelectedSourceRegion> Data = MakeShareable(new FSpriteSelectedSourceRegion());
                Data->SpritePtr = Sprite;
                Data->VertexIndex = 4 + VertexIndex;
                Canvas.SetHitProxy(new HSpriteSelectableObjectHitProxy(Data));
            }

            Canvas.DrawTile(MidPoint.X - EdgeCollisionVertexSize*0.5f, MidPoint.Y - EdgeCollisionVertexSize*0.5f, EdgeCollisionVertexSize, EdgeCollisionVertexSize, 0.f, 0.f, 1.f, 1.f, GeometryVertexColor, GWhiteTexture);

            if (bIsHitTesting)
            {
                Canvas.SetHitProxy(NULL);
            }
        }

        // Add corner hit proxy
        if (bDrawCornerHitProxies)
        {
            if (bIsHitTesting)
            {
                TSharedPtr<FSpriteSelectedSourceRegion> Data = MakeShareable(new FSpriteSelectedSourceRegion());
                Data->SpritePtr = Sprite;
                Data->VertexIndex = VertexIndex;
                Canvas.SetHitProxy(new HSpriteSelectableObjectHitProxy(Data));
            }
            
            Canvas.DrawTile(CornerPoint.X - CornerCollisionVertexSize * 0.5f, CornerPoint.Y - CornerCollisionVertexSize * 0.5f, CornerCollisionVertexSize, CornerCollisionVertexSize, 0.f, 0.f, 1.f, 1.f, GeometryVertexColor, GWhiteTexture);

            if (bIsHitTesting)
            {
                Canvas.SetHitProxy(NULL);
            }
        }
	}
}

void FSpriteEditorViewportClient::DrawGeometry(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, const FSpritePolygonCollection& Geometry, const FLinearColor& GeometryVertexColor, bool bIsRenderGeometry)
{
	const bool bIsHitTesting = Canvas.IsHitTesting();
	UPaperSprite* Sprite = GetSpriteBeingEdited();

	const float CollisionVertexSize = 8.0f;

	const float NormalLength = 15.0f;
	const FLinearColor GeometryLineColor(GeometryVertexColor.R, GeometryVertexColor.G, GeometryVertexColor.B, 0.5f * GeometryVertexColor.A);
	const FLinearColor GeometryNormalColor(0.0f, 1.0f, 0.0f, 0.5f);

	// Run thru the custom collision vertices and draw hit proxies for them
	for (int32 PolygonIndex = 0; PolygonIndex < Geometry.Polygons.Num(); ++PolygonIndex)
	{
		const FSpritePolygon& Polygon = Geometry.Polygons[PolygonIndex];

		// Draw lines connecting the vertices of the polygon
		for (int32 VertexIndex = 0; VertexIndex < Polygon.Vertices.Num(); ++VertexIndex)
		{
			const int32 NextVertexIndex = (VertexIndex + 1) % Polygon.Vertices.Num();

			const FVector2D ScreenPos = TextureSpaceToScreenSpace(View, Polygon.Vertices[VertexIndex]);
			const FVector2D NextScreenPos = TextureSpaceToScreenSpace(View, Polygon.Vertices[NextVertexIndex]);

			// Draw the normal tick
			if (bShowNormals)
			{
				const FVector2D Direction = (NextScreenPos - ScreenPos).SafeNormal();
				const FVector2D Normal = FVector2D(-Direction.Y, Direction.X);

				const FVector2D Midpoint = (ScreenPos + NextScreenPos) * 0.5f;
				const FVector2D NormalPoint = Midpoint - Normal * NormalLength;
				FCanvasLineItem LineItem(Midpoint, NormalPoint);
				LineItem.SetColor(GeometryNormalColor);
				Canvas.DrawItem(LineItem);
			}

			// Draw the edge
			{
				if (bIsHitTesting)
				{
					TSharedPtr<FSpriteSelectedEdge> Data = MakeShareable(new FSpriteSelectedEdge());
					Data->SpritePtr = Sprite;
					Data->PolygonIndex = PolygonIndex;
					Data->VertexIndex = VertexIndex;
					Data->bRenderData = bIsRenderGeometry;
					Canvas.SetHitProxy(new HSpriteSelectableObjectHitProxy(Data));
				}

				FCanvasLineItem LineItem(ScreenPos, NextScreenPos);
				LineItem.SetColor(GeometryLineColor);
				Canvas.DrawItem(LineItem);

				if (bIsHitTesting)
				{
					Canvas.SetHitProxy(NULL);
				}
			}
		}

		// Draw the vertices
		for (int32 VertexIndex = 0; VertexIndex < Polygon.Vertices.Num(); ++VertexIndex)
		{
			const FVector2D ScreenPos = TextureSpaceToScreenSpace(View, Polygon.Vertices[VertexIndex]);
			const float X = ScreenPos.X;
			const float Y = ScreenPos.Y;

			if (bIsHitTesting)
			{
				TSharedPtr<FSpriteSelectedVertex> Data = MakeShareable(new FSpriteSelectedVertex());
				Data->SpritePtr = Sprite;
				Data->PolygonIndex = PolygonIndex;
				Data->VertexIndex = VertexIndex;
				Data->bRenderData = bIsRenderGeometry;
				Canvas.SetHitProxy(new HSpriteSelectableObjectHitProxy(Data));
			}

			Canvas.DrawTile(ScreenPos.X - CollisionVertexSize*0.5f, ScreenPos.Y - CollisionVertexSize*0.5f, CollisionVertexSize, CollisionVertexSize, 0.f, 0.f, 1.f, 1.f, GeometryVertexColor, GWhiteTexture);
			
			if (bIsHitTesting)
			{
				Canvas.SetHitProxy(NULL);
			}
		}
	}
}

void FSpriteEditorViewportClient::DrawGeometryStats(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, const FSpritePolygonCollection& Geometry, bool bIsRenderGeometry, int32& YPos)
{
	// Draw the type of geometry we're displaying stats for
	const FText GeometryName = bIsRenderGeometry ? LOCTEXT("RenderGeometry", "Render Geometry (source)") : LOCTEXT("CollisionGeometry", "Collision Geometry (source)");

	FCanvasTextItem TextItem(FVector2D(6, YPos), GeometryName, GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);

	TextItem.Draw(&Canvas);
	TextItem.Position += FVector2D(6.0f, 18.0f);

	// Draw the number of polygons
	TextItem.Text = FText::Format(LOCTEXT("PolygonCount", "Polys: {0}"), FText::AsNumber(Geometry.Polygons.Num()));
	TextItem.Draw(&Canvas);
	TextItem.Position.Y += 18.0f;

	//@TODO: Should show the agg numbers instead for collision, and the render tris numbers for rendering
	// Draw the number of vertices
	int32 NumVerts = 0;
	for (int32 PolyIndex = 0; PolyIndex < Geometry.Polygons.Num(); ++PolyIndex)
	{
		NumVerts += Geometry.Polygons[PolyIndex].Vertices.Num();
	}

	TextItem.Text = FText::Format(LOCTEXT("VerticesCount", "Verts: {0}"), FText::AsNumber(NumVerts));
	TextItem.Draw(&Canvas);
	TextItem.Position.Y += 18.0f;

	YPos = (int32)TextItem.Position.Y;
}

void AttributeTrianglesByMaterialType(int32 NumTriangles, UMaterialInterface* Material, int32& NumOpaqueTriangles, int32& NumMaskedTriangles, int32& NumTranslucentTriangles)
{
	if (Material != nullptr)
	{
		switch (Material->GetBlendMode())
		{
		case EBlendMode::BLEND_Opaque:
			NumOpaqueTriangles += NumTriangles;
			break;
		case EBlendMode::BLEND_Translucent:
		case EBlendMode::BLEND_Additive:
		case EBlendMode::BLEND_Modulate:
			NumTranslucentTriangles += NumTriangles;
			break;
		case EBlendMode::BLEND_Masked:
			NumMaskedTriangles += NumTriangles;
			break;
		}
	}
}

void FSpriteEditorViewportClient::DrawRenderStats(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, class UPaperSprite* Sprite, int32& YPos)
{
	FCanvasTextItem TextItem(FVector2D(6, YPos), LOCTEXT("RenderGeomBaked", "Render Geometry (baked)"), GEngine->GetSmallFont(), FLinearColor::White);
	TextItem.EnableShadow(FLinearColor::Black);

	TextItem.Draw(&Canvas);
	TextItem.Position += FVector2D(6.0f, 18.0f);

	int32 NumSections = (Sprite->AlternateMaterialSplitIndex != INDEX_NONE) ? 2 : 1;
	if (NumSections > 1)
	{
		TextItem.Text = FText::Format(LOCTEXT("SectionCount", "Sections: {0}"), FText::AsNumber(NumSections));
		TextItem.Draw(&Canvas);
		TextItem.Position.Y += 18.0f;
	}

	int32 NumOpaqueTriangles = 0;
	int32 NumMaskedTriangles = 0;
	int32 NumTranslucentTriangles = 0;

	int32 NumVerts = Sprite->BakedRenderData.Num();
	int32 DefaultTriangles = 0;
	int32 AlternateTriangles = 0;
	if (Sprite->AlternateMaterialSplitIndex != INDEX_NONE)
	{
		DefaultTriangles = Sprite->AlternateMaterialSplitIndex / 3;
		AlternateTriangles = (NumVerts - Sprite->AlternateMaterialSplitIndex) / 3;
	}
	else
	{
		DefaultTriangles = NumVerts / 3;
	}

	AttributeTrianglesByMaterialType(DefaultTriangles, Sprite->GetDefaultMaterial(), /*inout*/ NumOpaqueTriangles, /*inout*/ NumMaskedTriangles, /*inout*/ NumTranslucentTriangles);
	AttributeTrianglesByMaterialType(AlternateTriangles, Sprite->GetAlternateMaterial(), /*inout*/ NumOpaqueTriangles, /*inout*/ NumMaskedTriangles, /*inout*/ NumTranslucentTriangles);

	// Draw the number of polygons
	if (NumOpaqueTriangles > 0)
	{
		TextItem.Text = FText::Format(LOCTEXT("OpaqueTriangleCount", "Triangles: {0} (opaque)"), FText::AsNumber(NumOpaqueTriangles));
		TextItem.Draw(&Canvas);
		TextItem.Position.Y += 18.0f;
	}

	if (NumMaskedTriangles > 0)
	{
		TextItem.Text = FText::Format(LOCTEXT("MaskedTriangleCount", "Triangles: {0} (masked)"), FText::AsNumber(NumMaskedTriangles));
		TextItem.Draw(&Canvas);
		TextItem.Position.Y += 18.0f;
	}

	if (NumTranslucentTriangles > 0)
	{
		TextItem.Text = FText::Format(LOCTEXT("TranslucentTriangleCount", "Triangles: {0} (translucent)"), FText::AsNumber(NumTranslucentTriangles));
		TextItem.Draw(&Canvas);
		TextItem.Position.Y += 18.0f;
	}

	YPos = (int32)TextItem.Position.Y;
}

void FSpriteEditorViewportClient::DrawBoundsAsText(FViewport& InViewport, FSceneView& View, FCanvas& Canvas, int32& YPos)
{
	FNumberFormattingOptions NoDigitGroupingFormat;
	NoDigitGroupingFormat.UseGrouping = false;

	UPaperSprite* Sprite = GetSpriteBeingEdited();
	FBoxSphereBounds Bounds = Sprite->GetRenderBounds();

	const FText DisplaySizeText = FText::Format(LOCTEXT("BoundsSize", "Approx. Size: {0}x{1}x{2}"),
		FText::AsNumber((int32)(Bounds.BoxExtent.X * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Y * 2.0f), &NoDigitGroupingFormat),
		FText::AsNumber((int32)(Bounds.BoxExtent.Z * 2.0f), &NoDigitGroupingFormat));

	Canvas.DrawShadowedString(
		6,
		YPos,
		*DisplaySizeText.ToString(),
		GEngine->GetSmallFont(),
		FLinearColor::White);
	YPos += 18;
}

void FSpriteEditorViewportClient::DrawSockets(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	UPaperSprite* Sprite = GetSpriteBeingEdited();

	const bool bIsHitTesting = PDI->IsHitTesting();

	const float DiamondSize = 5.0f;
	const FColor DiamondColor(255, 128, 128);

	const FTransform SpritePivotToWorld = Sprite->GetPivotToWorld();
	for (int32 SocketIndex = 0; SocketIndex < Sprite->Sockets.Num(); SocketIndex++)
	{
		const FPaperSpriteSocket& Socket = Sprite->Sockets[SocketIndex];
		const FMatrix SocketTM = (Socket.LocalTransform * SpritePivotToWorld).ToMatrixWithScale();
		
		if (bIsHitTesting)
		{
			TSharedPtr<FSpriteSelectedSocket> Data = MakeShareable(new FSpriteSelectedSocket);
			Data->SpritePtr = Sprite;
			Data->SocketName = Socket.SocketName;
			PDI->SetHitProxy(new HSpriteSelectableObjectHitProxy(Data));
		}
		
		DrawWireDiamond(PDI, SocketTM, DiamondSize, DiamondColor, SDPG_Foreground);
		
		PDI->SetHitProxy(NULL);
	}
}

void FSpriteEditorViewportClient::DrawSocketNames(FViewport& InViewport, FSceneView& View, FCanvas& Canvas)
{
	UPaperSprite* Sprite = GetSpriteBeingEdited();

	const int32 HalfX = Viewport->GetSizeXY().X / 2;
	const int32 HalfY = Viewport->GetSizeXY().Y / 2;

	const FColor SocketNameColor(255,196,196);

	// Draw socket names if desired.
	const FTransform SpritePivotToWorld = Sprite->GetPivotToWorld();
	for (int32 SocketIndex = 0; SocketIndex < Sprite->Sockets.Num(); SocketIndex++)
	{
		const FPaperSpriteSocket& Socket = Sprite->Sockets[SocketIndex];

		const FVector SocketWorldPos = SpritePivotToWorld.TransformPosition(Socket.LocalTransform.GetLocation());
		
		const FPlane Proj = View.Project(SocketWorldPos);
		if (Proj.W > 0.f)
		{
			const int32 XPos = HalfX + (HalfX * Proj.X);
			const int32 YPos = HalfY + (HalfY * (-Proj.Y));

			FCanvasTextItem Msg(FVector2D(XPos, YPos), FText::FromString(Socket.SocketName.ToString()), GEngine->GetMediumFont(), SocketNameColor);
			Canvas.DrawItem(Msg);

			//@TODO: Draws the current value of the rotation (probably want to keep this!)
// 			if (bManipulating && StaticMeshEditorPtr.Pin()->GetSelectedSocket() == Socket)
// 			{
// 				// Figure out the text height
// 				FTextSizingParameters Parameters(GEngine->GetSmallFont(), 1.0f, 1.0f);
// 				UCanvas::CanvasStringSize(Parameters, *Socket->SocketName.ToString());
// 				int32 YL = FMath::TruncToInt(Parameters.DrawYL);
// 
// 				DrawAngles(&Canvas, XPos, YPos + YL, 
// 					Widget->GetCurrentAxis(), 
// 					GetWidgetMode(),
// 					Socket->RelativeRotation,
// 					Socket->RelativeLocation);
// 			}
		}
	}
}

void FSpriteEditorViewportClient::DrawCanvas(FViewport& Viewport, FSceneView& View, FCanvas& Canvas)
{
	FEditorViewportClient::DrawCanvas(Viewport, View, Canvas);

	const bool bIsHitTesting = Canvas.IsHitTesting();
	if (!bIsHitTesting)
	{
		Canvas.SetHitProxy(NULL);
	}

	if (!SpriteEditorPtr.IsValid())
	{
		return;
	}

	UPaperSprite* Sprite = GetSpriteBeingEdited();

	int32 YPos = 42;

	static const FText GeomHelpStr = LOCTEXT("GeomEditHelp", "Select an edge and press Insert to add a vertex.\nSelect an edge or vertex and press Delete to remove it.\n");
	static const FText SourceRegionHelpStr = LOCTEXT("SourceRegionHelp", "Drag handles to adjust source region\nDouble-click on an image region to select all connected pixels");

	switch (CurrentMode)
	{
	case ESpriteEditorMode::ViewMode:
	default:

		// Display the pivot
		{
			FNumberFormattingOptions NoDigitGroupingFormat;
			NoDigitGroupingFormat.UseGrouping = false;
			const FText PivotStr = FText::Format(LOCTEXT("PivotPosition", "Pivot: ({0}, {1})"), FText::AsNumber(Sprite->CustomPivotPoint.X, &NoDigitGroupingFormat), FText::AsNumber(Sprite->CustomPivotPoint.Y, &NoDigitGroupingFormat));
			FCanvasTextItem TextItem(FVector2D(6, YPos), PivotStr, GEngine->GetSmallFont(), FLinearColor::White);
			TextItem.EnableShadow(FLinearColor::Black);
			TextItem.Draw(&Canvas);
			YPos += 18;
		}

		// Collision geometry (if present)
		if (Sprite->BodySetup != nullptr)
		{
			DrawGeometryStats(Viewport, View, Canvas, Sprite->CollisionGeometry, false, /*inout*/ YPos);
		}

		// Baked render data
		DrawRenderStats(Viewport, View, Canvas, Sprite, /*inout*/ YPos);

		// And bounds
		DrawBoundsAsText(Viewport, View, Canvas, /*inout*/ YPos);

		break;
	case ESpriteEditorMode::EditCollisionMode:
		{
 			// Display tool help
			{
				FCanvasTextItem TextItem(FVector2D(6, YPos), GeomHelpStr, GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);
				TextItem.Draw(&Canvas);
				YPos += 36;
			}

			// Draw the custom collision geometry
			const FLinearColor CollisionColor(1.0f, 1.0f, 0.0f, 1.0f);
			DrawGeometry(Viewport, View, Canvas, Sprite->CollisionGeometry, CollisionColor, false);
			if (Sprite->BodySetup != nullptr)
			{
				DrawGeometryStats(Viewport, View, Canvas, Sprite->CollisionGeometry, false, /*inout*/ YPos);
			}
		}
		break;
	case ESpriteEditorMode::EditRenderingGeomMode:
		{
 			// Display tool help
			{
				FCanvasTextItem TextItem(FVector2D(6, YPos), GeomHelpStr, GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);
				TextItem.Draw(&Canvas);
				YPos += 36;
			}

			// Draw the custom render geometry
			const FLinearColor RenderGeomColor(1.0f, 0.2f, 0.0f, 1.0f);
			DrawGeometry(Viewport, View, Canvas, Sprite->RenderGeometry, RenderGeomColor, true);
			DrawGeometryStats(Viewport, View, Canvas, Sprite->RenderGeometry, true, /*inout*/ YPos);
			DrawRenderStats(Viewport, View, Canvas, Sprite, /*inout*/ YPos);

			// And bounds
			DrawBoundsAsText(Viewport, View, Canvas, /*inout*/ YPos);
		}
		break;
	case ESpriteEditorMode::EditSourceRegionMode:
		{
			// Display tool help
			{
				FCanvasTextItem TextItem(FVector2D(6, YPos), SourceRegionHelpStr, GEngine->GetSmallFont(), FLinearColor::White);
				TextItem.EnableShadow(FLinearColor::Black);
				TextItem.Draw(&Canvas);
				YPos += 18;
			}

			// Draw the custom render geometry
			const FLinearColor BoundsColor(1.0f, 1.0f, 1.0f, 0.8f);
			DrawSourceRegion(Viewport, View, Canvas, BoundsColor, true);
		}
		break;
	case ESpriteEditorMode::AddSpriteMode:
		//@TODO:
		break;
	}

	if (bShowSockets)
	{
		DrawSocketNames(Viewport, View, Canvas);
	}
}

void FSpriteEditorViewportClient::Draw(const FSceneView* View, FPrimitiveDrawInterface* PDI)
{
	FEditorViewportClient::Draw(View, PDI);

	// We don't draw the pivot when showing the source region
	// The pivot may be outside the actual texture bounds there
	if (bShowPivot && !bShowSourceTexture && CurrentMode != ESpriteEditorMode::EditSourceRegionMode)
	{
		FUnrealEdUtils::DrawWidget(View, PDI, RenderSpriteComponent->ComponentToWorld.ToMatrixWithScale(), 0, 0, EAxisList::XZ, EWidgetMovementMode::WMM_Translate);
	}

	if (bShowSockets)
	{
		DrawSockets(View, PDI);
	}
}

void FSpriteEditorViewportClient::Draw(FViewport* Viewport, FCanvas* Canvas)
{
	FEditorViewportClient::Draw(Viewport, Canvas);
}

void FSpriteEditorViewportClient::Tick(float DeltaSeconds)
{
	if (UPaperSprite* Sprite = RenderSpriteComponent->GetSprite())
	{
		//@TODO: Doesn't need to happen every frame, only when properties are updated

		// Update the source texture view sprite (in case the texture has changed)
		UpdateSourceTextureSpriteFromSprite(Sprite);

		// Reposition the sprite (to be at the correct relative location to it's parent, undoing the pivot behavior)
		const FVector2D PivotInTextureSpace = Sprite->ConvertPivotSpaceToTextureSpace(FVector2D::ZeroVector);
		const FVector PivotInWorldSpace = TextureSpaceToWorldSpace(PivotInTextureSpace);
		RenderSpriteComponent->SetRelativeLocation(PivotInWorldSpace);

		bool bSourceTextureViewComponentVisibility = bShowSourceTexture || (CurrentMode == ESpriteEditorMode::EditSourceRegionMode);
		if (bSourceTextureViewComponentVisibility != SourceTextureViewComponent->IsVisible())
		{
			bDeferZoomToSprite = true;
			SourceTextureViewComponent->SetVisibility(bSourceTextureViewComponentVisibility);
		}

		bool bRenderTextureViewComponentVisibility = !bSourceTextureViewComponentVisibility;
		if (bRenderTextureViewComponentVisibility != RenderSpriteComponent->IsVisible())
		{
			bDeferZoomToSprite = true;
			RenderSpriteComponent->SetVisibility(bRenderTextureViewComponentVisibility);
		}

		// Zoom in on the sprite
		//@TODO: Fix this properly so it doesn't need to be deferred, or wait for the viewport to initialize
		FIntPoint Size = Viewport->GetSizeXY();
		if (bDeferZoomToSprite && (Size.X > 0) && (Size.Y > 0))
		{
			UPaperSpriteComponent* ComponentToFocusOn = SourceTextureViewComponent->IsVisible() ? SourceTextureViewComponent : RenderSpriteComponent;
			FocusViewportOnBox(ComponentToFocusOn->Bounds.GetBox(), true);
			bDeferZoomToSprite = false;
		}		
	}

	FPaperEditorViewportClient::Tick(DeltaSeconds);

	if (!GIntraFrameDebuggingGameThread)
	{
		OwnedPreviewScene.GetWorld()->Tick(LEVELTICK_All, DeltaSeconds);
	}
}

void FSpriteEditorViewportClient::ToggleShowSourceTexture()
{
	bShowSourceTexture = !bShowSourceTexture;
	SourceTextureViewComponent->SetVisibility(bShowSourceTexture);
	
	Invalidate();
}

void FSpriteEditorViewportClient::ToggleShowMeshEdges()
{
	EngineShowFlags.MeshEdges = !EngineShowFlags.MeshEdges;
	Invalidate();
}

bool FSpriteEditorViewportClient::IsShowMeshEdgesChecked() const
{
	return EngineShowFlags.MeshEdges;
}

void FSpriteEditorViewportClient::UpdateMouseDelta()
{
	FPaperEditorViewportClient::UpdateMouseDelta();
}

void FSpriteEditorViewportClient::ProcessClick(FSceneView& View, HHitProxy* HitProxy, FKey Key, EInputEvent Event, uint32 HitX, uint32 HitY)
{
	const FViewportClick Click(&View, this, Key, Event, HitX, HitY);
	if (HSpriteSelectableObjectHitProxy* SelectedItemProxy = HitProxyCast<HSpriteSelectableObjectHitProxy>(HitProxy))
	{
		const bool bIsCtrlKeyDown = Viewport->KeyState(EKeys::LeftControl) || Viewport->KeyState(EKeys::RightControl);
		if (!bIsCtrlKeyDown)
		{
			ClearSelectionSet();
		}

		SelectionSet.Add(SelectedItemProxy->Data);
	}
// 	else if (HWidgetUtilProxy* PivotProxy = HitProxyCast<HWidgetUtilProxy>(HitProxy))
// 	{
// 		//const bool bUserWantsPaint = bIsLeftButtonDown && ( !GetDefault<ULevelEditorViewportSettings>()->bLeftMouseDragMovesCamera ||  bIsCtrlDown );
// 		//findme
// 		WidgetAxis = WidgetProxy->Axis;
// 
// 			// Calculate the screen-space directions for this drag.
// 			FSceneViewFamilyContext ViewFamily( FSceneViewFamily::ConstructionValues( Viewport, GetScene(), EngineShowFlags ));
// 			FSceneView* View = CalcSceneView(&ViewFamily);
// 			WidgetProxy->CalcVectors(View, FViewportClick(View, this, Key, Event, HitX, HitY), LocalManipulateDir, WorldManipulateDir, DragX, DragY);
// 			bHandled = true;
// 	}
	else
	{
		ClearSelectionSet();

		if (Event == EInputEvent::IE_DoubleClick && CurrentMode == ESpriteEditorMode::EditSourceRegionMode)
		{
			FVector4 WorldPoint = View.PixelToWorld(HitX, HitY, 0);
			UPaperSprite* Sprite = GetSpriteBeingEdited();
			FVector2D TexturePoint = SourceTextureViewComponent->GetSprite()->ConvertWorldSpaceToTextureSpace(WorldPoint);
			Sprite->ExtractSourceRegionFromTexturePoint(TexturePoint);
		}

		FPaperEditorViewportClient::ProcessClick(View, HitProxy, Key, Event, HitX, HitY);
	}
}

bool FSpriteEditorViewportClient::InputKey(FViewport* Viewport, int32 ControllerId, FKey Key, EInputEvent Event, float AmountDepressed, bool bGamepad)
{
	bool bHandled = false;

	// Start the drag
	//@TODO: EKeys::LeftMouseButton
	//@TODO: Event.IE_Pressed
	// Implement InputAxis
	// StartTracking

	// Pass keys to standard controls, if we didn't consume input
	return (bHandled) ? true : FEditorViewportClient::InputKey(Viewport,  ControllerId, Key, Event, AmountDepressed, bGamepad);
}

bool FSpriteEditorViewportClient::InputWidgetDelta(FViewport* Viewport, EAxisList::Type CurrentAxis, FVector& Drag, FRotator& Rot, FVector& Scale)
{
	bool bHandled = false;
	if (bManipulating && (CurrentAxis != EAxisList::None))
	{
		bHandled = true;

		// Negate Y because vertices are in source texture space, not world space
		const FVector2D Drag2D(FVector::DotProduct(Drag, PaperAxisX), -FVector::DotProduct(Drag, PaperAxisY));

		// Apply the delta to all of the selected objects
		for (auto SelectionIt = SelectionSet.CreateConstIterator(); SelectionIt; ++SelectionIt)
		{
			TSharedPtr<FSelectedItem> SelectedItem = *SelectionIt;
			SelectedItem->ApplyDelta(Drag2D);
		}

		if (SelectionSet.Num() > 0)
		{
			if (!IsInSourceRegionEditMode())
			{
				UPaperSprite* Sprite = GetSpriteBeingEdited();
				// Sprite->RebuildCollisionData();
				// Sprite->RebuildRenderData();
				Sprite->PostEditChange();
				Invalidate();
			}

			bManipulationDirtiedSomething = true;
		}
	}

	return bHandled;
}

void FSpriteEditorViewportClient::TrackingStarted(const struct FInputEventState& InInputState, bool bIsDragging, bool bNudge)
{
	if (!bManipulating && bIsDragging)
	{
		BeginTransaction(LOCTEXT("ModificationInViewport", "Modification in Viewport"));
		bManipulating = true;
		bManipulationDirtiedSomething = false;
	}
}

void FSpriteEditorViewportClient::TrackingStopped() 
{
	if (bManipulating)
	{
		EndTransaction();
		bManipulating = false;
	}
}

FWidget::EWidgetMode FSpriteEditorViewportClient::GetWidgetMode() const
{
	return (SelectionSet.Num() > 0) ? FWidget::WM_Translate : FWidget::WM_None;
}

FVector FSpriteEditorViewportClient::GetWidgetLocation() const
{
	if (SelectionSet.Num() > 0)
	{
		UPaperSprite* Sprite = GetSpriteBeingEdited();

		// Find the center of the selection set
		FVector SummedPos(ForceInitToZero);
		for (auto SelectionIt = SelectionSet.CreateConstIterator(); SelectionIt; ++SelectionIt)
		{
			TSharedPtr<FSelectedItem> SelectedItem = *SelectionIt;
			SummedPos += SelectedItem->GetWorldPos();
		}
		return (SummedPos / SelectionSet.Num());
	}

	return FVector::ZeroVector;
}

FMatrix FSpriteEditorViewportClient::GetWidgetCoordSystem() const
{
	return FMatrix::Identity;
}

ECoordSystem FSpriteEditorViewportClient::GetWidgetCoordSystemSpace() const
{
	return COORD_World;
}

void FSpriteEditorViewportClient::BeginTransaction(const FText& SessionName)
{
	if (ScopedTransaction == NULL)
	{
		ScopedTransaction = new FScopedTransaction(SessionName);

		UPaperSprite* Sprite = GetSpriteBeingEdited();
		Sprite->Modify();
	}
}

void FSpriteEditorViewportClient::EndTransaction()
{
	if (bManipulationDirtiedSomething)
	{
		UPaperSprite* Sprite = RenderSpriteComponent->GetSprite();

		if (IsInSourceRegionEditMode())
		{
			// Snap to pixel grid at the end of the drag
			Sprite->SourceUV.X = FMath::Max(FMath::RoundToFloat(Sprite->SourceUV.X), 0.0f);
			Sprite->SourceUV.Y = FMath::Max(FMath::RoundToFloat(Sprite->SourceUV.Y), 0.0f);
			Sprite->SourceDimension.X = FMath::Max(FMath::RoundToFloat(Sprite->SourceDimension.X), 0.0f);
			Sprite->SourceDimension.Y = FMath::Max(FMath::RoundToFloat(Sprite->SourceDimension.Y), 0.0f);
		}

		Sprite->PostEditChange();
	}
	
	bManipulationDirtiedSomething = false;

	if (ScopedTransaction != NULL)
	{
		delete ScopedTransaction;
		ScopedTransaction = NULL;
	}
}

void FSpriteEditorViewportClient::NotifySpriteBeingEditedHasChanged()
{
	//@TODO: Ideally we do this before switching
	EndTransaction();
	ClearSelectionSet();

	// Update components to know about the new sprite being edited
	UPaperSprite* Sprite = GetSpriteBeingEdited();

	RenderSpriteComponent->SetSprite(Sprite);
	UpdateSourceTextureSpriteFromSprite(Sprite);

	//
	bDeferZoomToSprite = true;
}

void FSpriteEditorViewportClient::FocusOnSprite()
{
	FocusViewportOnBox(RenderSpriteComponent->Bounds.GetBox());
}

void FSpriteEditorViewportClient::DeleteSelection()
{
	BeginTransaction(LOCTEXT("DeleteSelectionTransaction", "Delete Selection"));

	//@TODO: Really need a proper data structure!!!
	if (SelectionSet.Num() == 1)
	{
		// Remove this item
		TSharedPtr<FSelectedItem> SelectedItem = *SelectionSet.CreateIterator();
		SelectedItem->Delete();
		ClearSelectionSet();
		bManipulationDirtiedSomething = true;
	}
	else
	{
		// Can't handle this without a better DS, as the indices will get screwed up
	}

	EndTransaction();
}

void FSpriteEditorViewportClient::SplitEdge()
{
	BeginTransaction(LOCTEXT("SplitEdgeTransaction", "Split Edge"));

	//@TODO: Really need a proper data structure!!!
	if (SelectionSet.Num() == 1)
	{
		// Split this item
		TSharedPtr<FSelectedItem> SelectedItem = *SelectionSet.CreateIterator();
		SelectedItem->SplitEdge();
		ClearSelectionSet();
		bManipulationDirtiedSomething = true;
	}
	else
	{
		// Can't handle this without a better DS, as the indices will get screwed up
	}

	EndTransaction();
}

void FSpriteEditorViewportClient::AddPolygon()
{
	BeginTransaction(LOCTEXT("AddPolygonTransaction", "Add Polygon"));

	if (FSpritePolygonCollection* Geometry = GetGeometryBeingEdited())
	{
		FSpritePolygon& NewPoly = *new (Geometry->Polygons) FSpritePolygon;

		//@TODO: Should make this more awesome (or even just regular awesome)
		// Hack adding verts since we don't have 'line drawing mode' where you can just click-click-click yet
		UPaperSprite* Sprite = GetSpriteBeingEdited();
		const FVector2D BaseUV = Sprite->GetSourceUV();
		const float Size = 10.0f;

		new (NewPoly.Vertices) FVector2D(BaseUV.X, BaseUV.Y);
		new (NewPoly.Vertices) FVector2D(BaseUV.X + Size, BaseUV.Y + Size);
		new (NewPoly.Vertices) FVector2D(BaseUV.X, BaseUV.Y + Size);
		bManipulationDirtiedSomething = true;

		Geometry->GeometryType = ESpritePolygonMode::FullyCustom;
	}

	EndTransaction();
}

void FSpriteEditorViewportClient::SnapAllVerticesToPixelGrid()
{
	BeginTransaction(LOCTEXT("SnapAllVertsToPixelGridTransaction", "Snap All Verts to Pixel Grid"));

	//@TODO: Doesn't handle snapping rectangle bounding boxes!
	//@TODO: Only OK because we don't allow manually editing them yet, and they're always on pixel grid when created automatically
	if (FSpritePolygonCollection* Geometry = GetGeometryBeingEdited())
	{
		for (int32 PolyIndex = 0; PolyIndex < Geometry->Polygons.Num(); ++PolyIndex)
		{
			FSpritePolygon& Poly = Geometry->Polygons[PolyIndex];
			for (int32 VertexIndex = 0; VertexIndex < Poly.Vertices.Num(); ++VertexIndex)
			{
				FVector2D& Vertex = Poly.Vertices[VertexIndex];

				Vertex.X = FMath::RoundToInt(Vertex.X);
				Vertex.Y = FMath::RoundToInt(Vertex.Y);
				bManipulationDirtiedSomething = true;
			}
		}
	}

	EndTransaction();
}

FSpritePolygonCollection* FSpriteEditorViewportClient::GetGeometryBeingEdited() const
{
	UPaperSprite* Sprite = GetSpriteBeingEdited();
	switch (CurrentMode)
	{
	case ESpriteEditorMode::EditCollisionMode:
		return &(Sprite->CollisionGeometry);
	case ESpriteEditorMode::EditRenderingGeomMode:
		return &(Sprite->RenderGeometry);
	default:
		return NULL;
	}
}

void FSpriteEditorViewportClient::ClearSelectionSet()
{
	SelectionSet.Empty();
}

//////////////////////////////////////////////////////////////////////////

#undef LOCTEXT_NAMESPACE