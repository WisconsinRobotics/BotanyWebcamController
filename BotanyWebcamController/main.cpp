/*
* Copyright (c) 2015, Wisconsin Robotics
* All rights reserved.
*
* Redistribution and use in source and binary forms, with or without
* modification, are permitted provided that the following conditions are met:
* * Redistributions of source code must retain the above copyright
*   notice, this list of conditions and the following disclaimer.
* * Redistributions in binary form must reproduce the above copyright
*   notice, this list of conditions and the following disclaimer in the
*   documentation and/or other materials provided with the distribution.
* * Neither the name of Wisconsin Robotics nor the
*   names of its contributors may be used to endorse or promote products
*   derived from this software without specific prior written permission.
*
* THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS" AND
* ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED
* WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE
* DISCLAIMED. IN NO EVENT SHALL WISCONSIN ROBOTICS BE LIABLE FOR ANY
* DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
* (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES;
* LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND
* ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
* (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS
* SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
*/

#include <winsock2.h>
#include <Ws2tcpip.h>
#include <Windows.h>
#include <mfapi.h>
#include <mfcaptureengine.h>
#include <mfreadwrite.h>
#include <Mferror.h>
#include <guiddef.h>
#include <stdio.h>
#include <wincodec.h>
#include <strsafe.h>
#include <stdint.h>

#define CAMERA_NAME L"Logitech HD Webcam C310"

#define INIT_CMD 'a'
#define TAKE_PHOTO_CMD 't'
#define PIC_DONE_CMD 'd'

IMFMediaSource *ppSource;
IMFSourceReader *pSourceReader;
IWICImagingFactory *pFactory = nullptr;

wchar_t picName[MAX_PATH];

template <class T> void SafeRelease(T **ppT)
{
	if (*ppT)
	{
		(*ppT)->Release();
		*ppT = nullptr;
	}
}

void CreateImage(BYTE* pData, DWORD currentLength)
{
	HRESULT hr;
	SYSTEMTIME systemTime;
	IWICBitmap *bitmap;
	IWICStream *pStream = nullptr;
	IWICBitmapEncoder *pEncoder = nullptr;
	IWICBitmapFrameEncode *pFrameEncode = nullptr;
	WICPixelFormatGUID pPixelFormat;

	GetLocalTime(&systemTime);
	//Year-Month-Date-Hour-Minute-Second-Camera
	StringCbPrintf(picName, MAX_PATH, L"%d-%d-%d-%d-%d-%d-A.jpg", systemTime.wYear, systemTime.wMonth, systemTime.wDay, systemTime.wHour, systemTime.wMinute, systemTime.wSecond);
	hr = pFactory->CreateBitmapFromMemory(
		640, 480,
		GUID_WICPixelFormat24bppRGB,
		1920, currentLength, pData, &bitmap);

	hr = pFactory->CreateStream(&pStream);
	hr = pStream->InitializeFromFilename(picName, GENERIC_WRITE);

	hr = pFactory->CreateEncoder(GUID_ContainerFormatJpeg, nullptr, &pEncoder);
	hr = pEncoder->Initialize(pStream, WICBitmapEncoderNoCache);
	hr = pEncoder->CreateNewFrame(&pFrameEncode, nullptr);
	hr = pFrameEncode->Initialize(nullptr);
	hr = pFrameEncode->SetSize(640, 480);
	bitmap->GetPixelFormat(&pPixelFormat);
	hr = pFrameEncode->SetPixelFormat(&pPixelFormat);

	hr = pFrameEncode->WriteSource(bitmap, NULL);
	hr = pFrameEncode->Commit();

	hr = pEncoder->Commit();
	pFrameEncode->Release();
	pEncoder->Release();
	bitmap->Release();
	pStream->Release();
}

int main()
{
	HRESULT hr;

	UINT32 count = 0;
	uint32_t index;
	DWORD streamCount = 0;
	DWORD streamIndex;
	DWORD streamFlags;
	LONGLONG timestamp;

	IMFAttributes *pConfig = nullptr;
	IMFAttributes *pSourceReaderConfig = nullptr;
	IMFActivate **ppDevices = nullptr;
	IMFPresentationDescriptor* pSourcePD = NULL;

	WORD requestVersion;
	WSADATA wsaData;
	SOCKET udpSocket;
	struct sockaddr_in targetAddress;
	struct sockaddr_in service;

	char sendBuffer;
	int result;

	requestVersion = MAKEWORD(2, 2);
	if (WSAStartup(requestVersion, &wsaData) != 0)
		return -1;

	udpSocket = socket(AF_INET, SOCK_DGRAM, IPPROTO_UDP);

	if (udpSocket == INVALID_SOCKET)
	{
		WSACleanup();
		return -1;
	}

	service.sin_family = AF_INET;
	InetPton(AF_INET, L"127.0.0.1", &(service.sin_addr.s_addr));
	service.sin_port = htons(10001);

	if (bind(udpSocket, (SOCKADDR *)&service, sizeof(service)) == SOCKET_ERROR)
	{
		WSACleanup();
		return -1;
	}

	hr = CoInitializeEx(NULL, COINIT_APARTMENTTHREADED);
	if (FAILED(hr))
	{
		WSACleanup();
		return -1;
	}

	MFStartup(MF_VERSION);

	hr = MFCreateAttributes(&pConfig, 1);
	if (FAILED(hr))
		goto Cleanup;

	hr = pConfig->SetGUID(
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE,
		MF_DEVSOURCE_ATTRIBUTE_SOURCE_TYPE_VIDCAP_GUID
		);

	if (FAILED(hr))
		goto Cleanup;

	hr = MFEnumDeviceSources(pConfig, &ppDevices, &count);

	for (index = 0; index < count; ++index)
	{
		WCHAR *szFriendlyName = NULL;
		UINT32 cchName;

		hr = ppDevices[index]->GetAllocatedString(
			MF_DEVSOURCE_ATTRIBUTE_FRIENDLY_NAME,
			&szFriendlyName, &cchName);

		if (SUCCEEDED(hr))
		{
			//wprintf_s(L"%s\n", szFriendlyName);
			if (wcscmp(szFriendlyName, CAMERA_NAME) == 0)
			{
				ppDevices[index]->ActivateObject(IID_PPV_ARGS(&ppSource));
				CoTaskMemFree(szFriendlyName);
				break;
			}
		}
		CoTaskMemFree(szFriendlyName);
	}


	hr = ppSource->CreatePresentationDescriptor(&pSourcePD);
	if (FAILED(hr))
		goto Cleanup;

	hr = MFCreateAttributes(&pSourceReaderConfig, 1);
	hr = pSourceReaderConfig->SetUINT32(MF_LOW_LATENCY, TRUE);

	hr = MFCreateSourceReaderFromMediaSource(ppSource, pSourceReaderConfig, &pSourceReader);

	DWORD mediaTypeIndex = 0;
	hr = S_OK;
	IMFMediaType *pType = NULL;
	while (SUCCEEDED(hr))
	{
		hr = pSourceReader->GetNativeMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, mediaTypeIndex, &pType);
		if (hr == MF_E_NO_MORE_TYPES)
		{
			hr = S_OK;
			break;
		}

		++mediaTypeIndex;

		if (SUCCEEDED(hr))
		{
			GUID mediaMajorGUID;
			GUID mediaSubGUID;

			UINT32 height;
			UINT32 width;
			UINT32 frameRateNum;
			UINT32 frameRateDenum;

			pType->GetMajorType(&mediaMajorGUID);
			pType->GetGUID(MF_MT_SUBTYPE, &mediaSubGUID);

			if (!IsEqualGUID(mediaSubGUID, MFVideoFormat_RGB24))
			{
				pType->Release();
				continue;
			}

			hr = MFGetAttributeSize(pType, MF_MT_FRAME_SIZE, &width, &height);
			//wprintf_s(L"%d by %d\n", width, height);
			if (width != 640 || height != 480)
			{
				pType->Release();
				continue;
			}


			MFGetAttributeRatio(pType, MF_MT_FRAME_RATE, &frameRateNum, &frameRateDenum);

			if (frameRateNum != 30 || frameRateDenum != 1)
			{
				pType->Release();
				continue;
			}

			hr = pSourceReader->SetCurrentMediaType(MF_SOURCE_READER_FIRST_VIDEO_STREAM, nullptr, pType);
			pType->Release();
			break;
		}
	}

	hr = CoCreateInstance(
		CLSID_WICImagingFactory,
		NULL,
		CLSCTX_INPROC_SERVER,
		IID_IWICImagingFactory,
		(LPVOID*)&pFactory
		);

	IMFSample *imfSample = nullptr;

	memset(&targetAddress, 0, sizeof(struct sockaddr_in));
	sendBuffer = INIT_CMD;
	targetAddress.sin_family = AF_INET;
	InetPton(AF_INET, L"127.0.0.1", &(targetAddress.sin_addr.s_addr));
	targetAddress.sin_port = htons(10000);
	result = sendto(udpSocket, (const char*)&sendBuffer, 1, 0, (const sockaddr*)&targetAddress, sizeof(targetAddress));

	while (true)
	{
		DWORD currentLength;
		char recBuffer;
		int fromLen;

		memset(&targetAddress, 0, sizeof(struct sockaddr_in));

		fromLen = sizeof(targetAddress);
		recvfrom(udpSocket, &recBuffer, 1, 0, (sockaddr*)&targetAddress, &fromLen);

		if (recBuffer == TAKE_PHOTO_CMD)
		{
			while (true)
			{
				IMFMediaBuffer *mediaBuffer = nullptr;
				BYTE *pData = nullptr;

				hr = pSourceReader->ReadSample(MF_SOURCE_READER_FIRST_VIDEO_STREAM, 0, &streamIndex, &streamFlags, &timestamp, &imfSample);
				if (!imfSample)
					continue;

				imfSample->ConvertToContiguousBuffer(&mediaBuffer);
				mediaBuffer->Lock(&pData, NULL, &currentLength);

				if (currentLength == 0)
					continue;

				CreateImage(pData, currentLength);

				mediaBuffer->Unlock();
				mediaBuffer->Release();
				imfSample->Release();

				pSourceReader->Flush(MF_SOURCE_READER_FIRST_VIDEO_STREAM);
				sendBuffer = PIC_DONE_CMD;
				result = sendto(udpSocket, (const char*)&sendBuffer, 1, 0, (const sockaddr*)&targetAddress, sizeof(targetAddress));
				break;
			}
		}
	}

Cleanup:
	if (ppDevices != nullptr)
		CoTaskMemFree(ppDevices);
	if (pConfig != nullptr)
		pConfig->Release();
	if (pFactory != nullptr)
		pFactory->Release();
	WSACleanup();
	MFShutdown();

	return 0;
}