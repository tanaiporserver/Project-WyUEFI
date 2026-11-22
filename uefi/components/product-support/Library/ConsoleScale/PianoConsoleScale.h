// SPDX-License-Identifier: BSD-2-Clause-Patent
// Private text-console surface. The published GOP and physical mode remain
// unchanged; only GraphicsConsole's character rendering uses logical pixels.
typedef struct {
  EFI_GRAPHICS_OUTPUT_PROTOCOL Gop;
  EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE Mode;
  EFI_GRAPHICS_OUTPUT_MODE_INFORMATION Info;
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Physical;
  UINTN Scale;
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Scratch;
  UINTN ScratchPixels;
} PIANO_CONSOLE_SURFACE;

STATIC EFI_STATUS EFIAPI PianoConsoleQuery(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  UINT32 Mode,UINTN *Bytes,EFI_GRAPHICS_OUTPUT_MODE_INFORMATION **Info){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;
  if(!Bytes||!Info||Mode!=S->Mode.Mode)return EFI_UNSUPPORTED;
  *Info=AllocateCopyPool(sizeof(S->Info),&S->Info);
  if(!*Info)return EFI_OUT_OF_RESOURCES;
  *Bytes=sizeof(S->Info);return EFI_SUCCESS;
}
STATIC EFI_STATUS EFIAPI PianoConsoleSet(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,UINT32 Mode){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;
  return Mode==S->Mode.Mode?EFI_SUCCESS:EFI_UNSUPPORTED;
}
STATIC EFI_STATUS EFIAPI PianoConsoleBlt(EFI_GRAPHICS_OUTPUT_PROTOCOL *This,
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Buffer,EFI_GRAPHICS_OUTPUT_BLT_OPERATION Op,
  UINTN Sx,UINTN Sy,UINTN Dx,UINTN Dy,UINTN W,UINTN H,UINTN Delta){
  PIANO_CONSOLE_SURFACE *S=(VOID *)This;UINTN N=S->Scale;
  if(!W||!H||Op>=EfiGraphicsOutputBltOperationMax)return EFI_INVALID_PARAMETER;
  if(Op!=EfiBltVideoToBltBuffer &&
     (Dx>=S->Info.HorizontalResolution||Dy>=S->Info.VerticalResolution||
      W>S->Info.HorizontalResolution-Dx||H>S->Info.VerticalResolution-Dy))
    return EFI_INVALID_PARAMETER;
  if(Op==EfiBltVideoFill)
    return S->Physical->Blt(S->Physical,Buffer,Op,0,0,Dx*N,Dy*N,W*N,H*N,0);
  if(Op==EfiBltVideoToVideo)
    return S->Physical->Blt(S->Physical,NULL,Op,Sx*N,Sy*N,Dx*N,Dy*N,W*N,H*N,0);
  if(!Buffer||W>MAX_UINTN/N||H>MAX_UINTN/N)return EFI_INVALID_PARAMETER;
  UINTN RowPixels=W*N,Rows=H*N;
  if(RowPixels>MAX_UINTN/sizeof(*Buffer)/Rows)return EFI_BAD_BUFFER_SIZE;
  if(Delta==0)Delta=W*sizeof(*Buffer);
  if(Delta%sizeof(*Buffer)||Delta/sizeof(*Buffer)<(Op==EfiBltBufferToVideo?Sx:Dx)+W)
    return EFI_INVALID_PARAMETER;
  UINTN NeedPixels=RowPixels*Rows;
  // นำบัฟเฟอร์สเกลกลับมาใช้ซ้ำเพื่อลดการจองและคืนหน่วยความจำทุกตัวอักษรใน UEFI Setup/Shell
  if(NeedPixels>S->ScratchPixels){
    EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Next=AllocatePool(NeedPixels*sizeof(*Buffer));
    if(!Next)return EFI_OUT_OF_RESOURCES;
    if(S->Scratch)FreePool(S->Scratch);
    S->Scratch=Next;S->ScratchPixels=NeedPixels;
  }
  EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Scaled=S->Scratch;
  EFI_STATUS Status;
  if(Op==EfiBltBufferToVideo){
    UINTN RowBytes=RowPixels*sizeof(*Buffer);
    for(UINTN SrcY=0;SrcY<H;++SrcY){
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Source=(VOID *)((UINT8 *)Buffer+(Sy+SrcY)*Delta);
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *FirstRow=Scaled+(SrcY*N)*RowPixels;
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dst=FirstRow;
      // ขยายแนวนอนแบบไม่ต้องหารด้วย N และทำซ้ำแถวที่เหลือด้วย CopyMem
      if(N==3){
        for(UINTN SrcX=0;SrcX<W;++SrcX){
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px=Source[Sx+SrcX];
          Dst[0]=Px;Dst[1]=Px;Dst[2]=Px;Dst+=3;
        }
      }else if(N==4){
        for(UINTN SrcX=0;SrcX<W;++SrcX){
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px=Source[Sx+SrcX];
          Dst[0]=Px;Dst[1]=Px;Dst[2]=Px;Dst[3]=Px;Dst+=4;
        }
      }else{
        for(UINTN SrcX=0;SrcX<W;++SrcX){
          EFI_GRAPHICS_OUTPUT_BLT_PIXEL Px=Source[Sx+SrcX];
          for(UINTN K=0;K<N;++K)*Dst++=Px;
        }
      }
      for(UINTN Dup=1;Dup<N;++Dup){
        CopyMem(FirstRow+Dup*RowPixels,FirstRow,RowBytes);
      }
    }
    Status=S->Physical->Blt(S->Physical,Scaled,Op,0,0,Dx*N,Dy*N,RowPixels,Rows,RowBytes);
  }else{
    Status=S->Physical->Blt(S->Physical,Scaled,Op,Sx*N,Sy*N,0,0,RowPixels,Rows,RowPixels*sizeof(*Buffer));
    if(!EFI_ERROR(Status))for(UINTN Y=0;Y<H;++Y){
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *Dest=(VOID *)((UINT8 *)Buffer+(Dy+Y)*Delta);
      EFI_GRAPHICS_OUTPUT_BLT_PIXEL *SrcRow=Scaled+(Y*N)*RowPixels;
      for(UINTN X=0;X<W;++X)Dest[Dx+X]=SrcRow[X*N];
    }
  }
  return Status;
}
STATIC EFI_STATUS PianoConsoleAttach(GRAPHICS_CONSOLE_DEV *Private,UINT32 *Width,UINT32 *Height){
  EFI_GRAPHICS_OUTPUT_PROTOCOL *Physical=Private->GraphicsOutput;
  UINT32 Scale=0;
  if(!Physical)return EFI_SUCCESS;
  // ปรับสเกลข้อความคอนโซลและ UiApp ตามความละเอียดแผงจอจริงของแต่ละรุ่น
  if(*Width==3200&&*Height==2136)Scale=4;
  else if(*Width==2400&&*Height==1504)Scale=3;
  else return EFI_SUCCESS;
  PIANO_CONSOLE_SURFACE *S=AllocateZeroPool(sizeof(*S));
  if(!S)return EFI_OUT_OF_RESOURCES;
  S->Physical=Physical;S->Scale=Scale;
  S->Mode=*Physical->Mode;S->Info=*Physical->Mode->Info;
  S->Info.HorizontalResolution=*Width/Scale;S->Info.VerticalResolution=*Height/Scale;
  S->Info.PixelsPerScanLine=S->Info.HorizontalResolution;
  S->Info.PixelFormat=PixelBltOnly;
  S->Mode.Info=&S->Info;S->Mode.SizeOfInfo=sizeof(S->Info);
  S->Mode.FrameBufferBase=0;S->Mode.FrameBufferSize=0;
  S->Gop=(EFI_GRAPHICS_OUTPUT_PROTOCOL){PianoConsoleQuery,PianoConsoleSet,PianoConsoleBlt,&S->Mode};
  Private->GraphicsOutput=&S->Gop;Private->PianoConsoleSurface=S;
  DEBUG((DEBUG_INFO,"PIANO_CONSOLE_SCALE factor=%u physical=%ux%u logical=%ux%u\n",
    Scale,*Width,*Height,S->Info.HorizontalResolution,S->Info.VerticalResolution));
  *Width=S->Info.HorizontalResolution;*Height=S->Info.VerticalResolution;
  return EFI_SUCCESS;
}
STATIC VOID PianoConsoleDetach(GRAPHICS_CONSOLE_DEV *Private){
  PIANO_CONSOLE_SURFACE *S=Private->PianoConsoleSurface;
  if(S){
    if(S->Scratch)FreePool(S->Scratch);
    Private->GraphicsOutput=S->Physical;Private->PianoConsoleSurface=NULL;FreePool(S);
  }
}
