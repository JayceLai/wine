/*
 * Copyright 2019 Dmitry Timoshkov
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA 02110-1301, USA
 */

#include <stdarg.h>

#define COBJMACROS
#define NONAMELESSSTRUCT
#define NONAMELESSUNION

#include "windef.h"
#include "winbase.h"
#include "wingdi.h"
#include "winspool.h"
#include "objbase.h"
#include "prntvpt.h"
#include "initguid.h"
#include "msxml2.h"
#include "wine/heap.h"
#include "wine/debug.h"

#include "prntvpt_private.h"

WINE_DEFAULT_DEBUG_CHANNEL(prntvpt);

struct size
{
    int width;
    int height;
};

struct media
{
    int paper;
    struct size size;
};

struct resolution
{
    int x;
    int y;
};

struct page
{
    struct media media;
    struct resolution resolution;
    int orientation;
    int scaling;
    int color;
};

struct document
{
    int collate;
};

struct job
{
    int nup;
    int copies;
    int input_bin;
};

struct ticket
{
    struct job job;
    struct document document;
    struct page page;
};

static const struct
{
    const WCHAR *name;
    int paper;
} psk_media[] =
{
    { L"psk:ISOA4", DMPAPER_A4 },
};

static int media_to_paper(const WCHAR *name)
{
    int i;

    for (i = 0 ; i < ARRAY_SIZE(psk_media); i++)
        if (!wcscmp(name, psk_media[i].name))
            return psk_media[i].paper;

    FIXME("%s\n", wine_dbgstr_w(name));
    return DMPAPER_A4;
}

static const WCHAR *paper_to_media(int paper)
{
    int i;

    for (i = 0 ; i < ARRAY_SIZE(psk_media); i++)
        if (psk_media[i].paper == paper)
            return psk_media[i].name;

    FIXME("%d\n", paper);
    return psk_media[0].name;
}

static BOOL is_valid_node_name(const WCHAR *name)
{
    static const WCHAR * const psf_names[] = { L"psf:ParameterInit", L"psf:Feature" };
    int i;

    for (i = 0 ; i < ARRAY_SIZE(psf_names); i++)
        if (!wcscmp(name, psf_names[i])) return TRUE;

    return FALSE;
}

static HRESULT verify_ticket(IXMLDOMDocument2 *doc)
{
    IXMLDOMElement *element;
    IXMLDOMNode *node = NULL;
    BSTR str;
    HRESULT hr;

    hr = IXMLDOMDocument2_get_documentElement(doc, &element);
    if (hr != S_OK) return E_PRINTTICKET_FORMAT;

    hr = IXMLDOMElement_get_tagName(element, &str);
    if (hr != S_OK) goto fail;
    if (wcscmp(str, L"psf:PrintTicket") != 0)
        hr = E_FAIL;
    SysFreeString(str);
    if (hr != S_OK) goto fail;

    hr = IXMLDOMElement_get_firstChild(element, &node);
    IXMLDOMElement_Release(element);
    if (hr != S_OK) return S_OK;

    for (;;)
    {
        VARIANT var;
        IXMLDOMNode *next_node;

        hr = IXMLDOMNode_get_nodeName(node, &str);
        if (hr != S_OK) break;
        if (!is_valid_node_name(str))
            hr = E_FAIL;
        SysFreeString(str);
        if (hr != S_OK) break;

        hr = IXMLDOMNode_QueryInterface(node, &IID_IXMLDOMElement, (void **)&element);
        if (hr != S_OK) break;

        VariantInit(&var);
        hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
        IXMLDOMElement_Release(element);
        if (hr != S_OK) break;
        if (V_VT(&var) != VT_BSTR)
            hr = E_FAIL;
        VariantClear(&var);
        if (hr != S_OK) break;

        hr = IXMLDOMNode_get_nextSibling(node, &next_node);
        if (hr != S_OK)
        {
            hr = S_OK;
            break;
        }

        IXMLDOMNode_Release(node);
        node = next_node;
    }

fail:
    if (node) IXMLDOMNode_Release(node);

    return hr != S_OK ? E_PRINTTICKET_FORMAT : S_OK;
}

static HRESULT read_int_value(IXMLDOMNode *node, int *value)
{
    IXMLDOMNode *val;
    HRESULT hr;
    VARIANT var1, var2;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Value[@xsi:type='xsd:integer']", &val);
    if (hr != S_OK) return hr;

    VariantInit(&var1);
    hr = IXMLDOMNode_get_nodeTypedValue(val, &var1);
    if (hr == S_OK)
    {
        VariantInit(&var2);
        hr = VariantChangeTypeEx(&var2, &var1, 0, 0, VT_I4);
        if (hr == S_OK)
            *value = V_I4(&var2);

        VariantClear(&var1);
    }

    IXMLDOMNode_Release(val);
    return hr;
}

static void read_PageMediaSize(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option, *child;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:PageMediaSize']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                ticket->page.media.paper = media_to_paper(V_BSTR(&var));
                TRACE("paper: %s => %d\n", wine_dbgstr_w(V_BSTR(&var)), ticket->page.media.paper);
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }

        hr = IXMLDOMNode_selectSingleNode(option, (BSTR)L"./psf:ScoredProperty[@name='psk:MediaSizeWidth']", &child);
        if (hr == S_OK)
        {
            if (read_int_value(child, &ticket->page.media.size.width) == S_OK)
                TRACE("width: %d\n", ticket->page.media.size.width);
            IXMLDOMNode_Release(child);
        }

        hr = IXMLDOMNode_selectSingleNode(option, (BSTR)L"./psf:ScoredProperty[@name='psk:MediaSizeHeight']", &child);
        if (hr == S_OK)
        {
            if (read_int_value(child, &ticket->page.media.size.height) == S_OK)
                TRACE("height: %d\n", ticket->page.media.size.height);
            IXMLDOMNode_Release(child);
        }

        IXMLDOMNode_Release(option);
    }

    IXMLDOMNode_Release(node);
}

static void read_PageOutputColor(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:PageOutputColor']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                if (!wcscmp(V_BSTR(&var), L"psk:Color"))
                    ticket->page.color = DMCOLOR_COLOR;
                else if (!wcscmp(V_BSTR(&var), L"psk:Monochrome"))
                    ticket->page.color = DMCOLOR_MONOCHROME;
                else
                {
                    FIXME("%s\n", wine_dbgstr_w(V_BSTR(&var)));
                    ticket->page.color = DMCOLOR_MONOCHROME;
                }
                TRACE("color: %s => %d\n", wine_dbgstr_w(V_BSTR(&var)), ticket->page.color);
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }
    }

    IXMLDOMNode_Release(node);
}

static void read_PageScaling(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option;
    int scaling = 0;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:PageScaling']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                if (!wcscmp(V_BSTR(&var), L"psk:None"))
                    scaling = 100;
                else if (!wcscmp(V_BSTR(&var), L"psk:CustomSquare"))
                    scaling = 0; /* psk:PageScalingScale */
                else
                    FIXME("%s\n", wine_dbgstr_w(V_BSTR(&var)));
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }
    }

    IXMLDOMNode_Release(node);

    if (!scaling)
    {
        hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:ParameterInit[@name='psk:PageScalingScale']", &node);
        if (hr == S_OK)
        {
            read_int_value(node, &scaling);
            IXMLDOMNode_Release(node);
        }
    }

    if (scaling)
        ticket->page.scaling = scaling;
    else
        ticket->page.scaling = 100;

    TRACE("page.scaling: %d\n", ticket->page.scaling);
}

static void read_PageResolution(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option, *child;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:PageResolution']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        hr = IXMLDOMNode_selectSingleNode(option, (BSTR)L"./psf:ScoredProperty[@name='psk:ResolutionX']", &child);
        if (hr == S_OK)
        {
            if (read_int_value(child, &ticket->page.resolution.x) == S_OK)
                TRACE("resolution.x: %d\n", ticket->page.resolution.x);
            IXMLDOMNode_Release(child);
        }

        hr = IXMLDOMNode_selectSingleNode(option, (BSTR)L"./psf:ScoredProperty[@name='psk:ResolutionY']", &child);
        if (hr == S_OK)
        {
            if (read_int_value(child, &ticket->page.resolution.y) == S_OK)
                TRACE("resolution.y: %d\n", ticket->page.resolution.y);
            IXMLDOMNode_Release(child);
        }

        IXMLDOMNode_Release(option);
    }

    IXMLDOMNode_Release(node);
}

static void read_PageOrientation(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:PageOrientation']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                if (!wcscmp(V_BSTR(&var), L"psk:Portrait"))
                    ticket->page.orientation = DMORIENT_PORTRAIT;
                else if (!wcscmp(V_BSTR(&var), L"psk:Landscape"))
                    ticket->page.orientation = DMORIENT_LANDSCAPE;
                else
                {
                    FIXME("%s\n", wine_dbgstr_w(V_BSTR(&var)));
                    ticket->page.orientation = DMORIENT_PORTRAIT;
                }
                TRACE("orientation: %s => %d\n", wine_dbgstr_w(V_BSTR(&var)), ticket->page.orientation);
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }
    }

    IXMLDOMNode_Release(node);
}

static void read_DocumentCollate(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:DocumentCollate']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                if (!wcscmp(V_BSTR(&var), L"psk:Collated"))
                    ticket->document.collate = DMCOLLATE_TRUE;
                else if (!wcscmp(V_BSTR(&var), L"psk:Uncollated"))
                    ticket->document.collate = DMCOLLATE_FALSE;
                else
                {
                    FIXME("%s\n", wine_dbgstr_w(V_BSTR(&var)));
                    ticket->document.collate = DMCOLLATE_FALSE;
                }
                TRACE("document.collate: %s => %d\n", wine_dbgstr_w(V_BSTR(&var)), ticket->document.collate);
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }
    }

    IXMLDOMNode_Release(node);
}

static void read_JobInputBin(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node, *option;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:Feature[@name='psk:JobInputBin']", &node);
    if (hr != S_OK) return;

    hr = IXMLDOMNode_selectSingleNode(node, (BSTR)L"./psf:Option", &option);
    if (hr == S_OK)
    {
        IXMLDOMElement *element;

        hr = IXMLDOMNode_QueryInterface(option, &IID_IXMLDOMElement, (void **)&element);
        if (hr == S_OK)
        {
            VARIANT var;

            VariantInit(&var);
            hr = IXMLDOMElement_getAttribute(element, (BSTR)L"name", &var);
            if (hr == S_OK && V_VT(&var) == VT_BSTR)
            {
                if (!wcscmp(V_BSTR(&var), L"psk:AutoSelect"))
                    ticket->job.input_bin = DMBIN_AUTO;
                else
                {
                    FIXME("%s\n", wine_dbgstr_w(V_BSTR(&var)));
                    ticket->job.input_bin = DMBIN_AUTO;
                }
                TRACE("input_bin: %s => %d\n", wine_dbgstr_w(V_BSTR(&var)), ticket->job.input_bin);
            }
            VariantClear(&var);

            IXMLDOMElement_Release(element);
        }
    }

    IXMLDOMNode_Release(node);
}

static void read_JobCopies(IXMLDOMDocument2 *doc, struct ticket *ticket)
{
    IXMLDOMNode *node;
    HRESULT hr;

    hr = IXMLDOMDocument2_selectSingleNode(doc, (BSTR)L"psf:PrintTicket/psf:ParameterInit[@name='psk:JobCopiesAllDocuments']", &node);
    if (hr != S_OK) return;

    if (read_int_value(node, &ticket->job.copies) == S_OK)
        TRACE("job.copies: %d\n", ticket->job.copies);

    IXMLDOMNode_Release(node);
}

static void set_SelectionNamespaces(IXMLDOMDocument2 *doc)
{
    IStream *stream;
    IXMLDOMElement *element = NULL;
    IXMLDOMNamedNodeMap *map = NULL;
    HRESULT hr;
    LONG count, i;
    HGLOBAL hmem;
    BSTR str;
    VARIANT var;

    hr = CreateStreamOnHGlobal(NULL, TRUE, &stream);
    if (hr != S_OK) return;

    hr = IXMLDOMDocument2_get_documentElement(doc, &element);
    if (hr != S_OK) goto fail;

    hr = IXMLDOMElement_get_attributes(element, &map);
    if (hr != S_OK) goto fail;

    hr = IXMLDOMNamedNodeMap_get_length(map, &count);
    if (hr != S_OK) goto fail;

    for (i = 0; i < count; i++)
    {
        IXMLDOMNode *node;

        hr = IXMLDOMNamedNodeMap_get_item(map, i, &node);
        if (hr == S_OK)
        {
            hr = IXMLDOMNode_get_nodeName(node, &str);
            if (hr == S_OK)
            {
                VariantInit(&var);
                hr = IXMLDOMNode_get_nodeValue(node, &var);
                if (hr == S_OK)
                {
                    if (!wcscmp(str, L"xmlns") || !wcsncmp(str, L"xmlns:", 6))
                    {
                        TRACE("ns[%d]: %s=%s\n", i, wine_dbgstr_w(str), wine_dbgstr_w(V_BSTR(&var)));
                        IStream_Write(stream, str, wcslen(str) * sizeof(WCHAR), NULL);
                        IStream_Write(stream, L"=\"", 2 * sizeof(WCHAR), NULL);
                        IStream_Write(stream, V_BSTR(&var), wcslen(V_BSTR(&var)) * sizeof(WCHAR), NULL);
                        IStream_Write(stream, L"\" ", 2 * sizeof(WCHAR), NULL);
                    }
                    VariantClear(&var);
                }
                SysFreeString(str);
            }
            IXMLDOMNode_Release(node);
        }
    }

    IStream_Write(stream, L"", sizeof(WCHAR), NULL);

    hr = GetHGlobalFromStream(stream, &hmem);
    if (hr != S_OK) goto fail;

    str = GlobalLock(hmem);
    V_VT(&var) = VT_BSTR;
    V_BSTR(&var) = SysAllocString(str);
    IXMLDOMDocument2_setProperty(doc, (BSTR)L"SelectionNamespaces", var);
    SysFreeString(V_BSTR(&var));
    GlobalUnlock(hmem);

fail:
    if (map) IXMLDOMNamedNodeMap_Release(map);
    if (element) IXMLDOMElement_Release(element);
    IStream_Release(stream);
}

static HRESULT parse_ticket(IStream *stream, EPrintTicketScope scope, struct ticket *ticket)
{
    IXMLDOMDocument2 *doc;
    VARIANT src;
    VARIANT_BOOL ret;
    HRESULT hr;

    hr = CoCreateInstance(&CLSID_DOMDocument30, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IXMLDOMDocument2, (void **)&doc);
    if (hr != S_OK) return hr;

    V_VT(&src) = VT_UNKNOWN;
    V_UNKNOWN(&src) = (IUnknown *)stream;
    hr = IXMLDOMDocument2_load(doc, src, &ret);
    if (hr != S_OK) goto fail;

    hr = verify_ticket(doc);
    if (hr != S_OK) goto fail;

    set_SelectionNamespaces(doc);

    /* PageScope is always added */
    read_PageMediaSize(doc, ticket);
    read_PageOutputColor(doc, ticket);
    read_PageScaling(doc, ticket);
    read_PageResolution(doc, ticket);
    read_PageOrientation(doc, ticket);

    if (scope > kPTPageScope)
        read_DocumentCollate(doc, ticket);

    if (scope > kPTDocumentScope)
    {
        read_JobInputBin(doc, ticket);
        read_JobCopies(doc, ticket);
    }

fail:
    IXMLDOMDocument2_Release(doc);
    return hr;
}

static void ticket_to_devmode(const struct ticket *ticket, DEVMODEW *dm)
{
    memset(dm, 0, sizeof(*dm));

    dm->dmSize = sizeof(*dm);
    dm->dmFields = DM_ORIENTATION | DM_PAPERSIZE | DM_PAPERLENGTH | DM_PAPERWIDTH | DM_SCALE |
                   DM_COPIES | DM_COLOR | DM_PRINTQUALITY | DM_YRESOLUTION | DM_COLLATE;
    dm->u1.s1.dmOrientation = ticket->page.orientation;
    dm->u1.s1.dmPaperSize = ticket->page.media.paper;
    dm->u1.s1.dmPaperWidth = ticket->page.media.size.width / 100;
    dm->u1.s1.dmPaperLength = ticket->page.media.size.height / 100;
    dm->u1.s1.dmScale = ticket->page.scaling;
    dm->u1.s1.dmCopies = ticket->job.copies;
    dm->dmColor = ticket->page.color;
    dm->u1.s1.dmPrintQuality = ticket->page.resolution.x;
    dm->dmYResolution = ticket->page.resolution.y;
    dm->dmCollate = ticket->document.collate;
}

static void devmode_to_ticket(const DEVMODEW *dm, struct ticket *ticket)
{
    if (dm->dmFields & DM_ORIENTATION)
        ticket->page.orientation = dm->u1.s1.dmOrientation;
    if (dm->dmFields & DM_PAPERSIZE)
        ticket->page.media.paper = dm->u1.s1.dmPaperSize;
    if (dm->dmFields & DM_PAPERLENGTH)
        ticket->page.media.size.width = dm->u1.s1.dmPaperWidth * 100;
    if (dm->dmFields & DM_PAPERWIDTH)
        ticket->page.media.size.height = dm->u1.s1.dmPaperLength * 100;
    if (dm->dmFields & DM_SCALE)
        ticket->page.scaling = dm->u1.s1.dmScale;
    if (dm->dmFields & DM_COPIES)
        ticket->job.copies = dm->u1.s1.dmCopies;
    if (dm->dmFields & DM_COLOR)
        ticket->page.color = dm->dmColor;
    if (dm->dmFields & DM_PRINTQUALITY)
    {
        ticket->page.resolution.x = dm->u1.s1.dmPrintQuality;
        ticket->page.resolution.y = dm->u1.s1.dmPrintQuality;
    }
    if (dm->dmFields & DM_YRESOLUTION)
        ticket->page.resolution.y = dm->dmYResolution;
    if (dm->dmFields & DM_LOGPIXELS)
    {
        ticket->page.resolution.x = dm->dmLogPixels;
        ticket->page.resolution.y = dm->dmLogPixels;
    }
    if (dm->dmFields & DM_COLLATE)
        ticket->document.collate = dm->dmCollate;
}

static HRESULT initialize_ticket(struct prn_provider *prov, struct ticket *ticket)
{
    PRINTER_INFO_2W *pi2;
    DWORD size;
    HRESULT hr = S_OK;

    GetPrinterW(prov->hprn, 2, NULL, 0, &size);
    if (GetLastError() != ERROR_INSUFFICIENT_BUFFER)
        return HRESULT_FROM_WIN32(GetLastError());

    pi2 = heap_alloc(size);
    if (!pi2) return E_OUTOFMEMORY;

    if (!GetPrinterW(prov->hprn, 2, (LPBYTE)pi2, size, NULL))
        hr = HRESULT_FROM_WIN32(GetLastError());
    else
        devmode_to_ticket(pi2->pDevMode, ticket);

    heap_free(pi2);
    return hr;
}

HRESULT WINAPI PTConvertPrintTicketToDevMode(HPTPROVIDER provider, IStream *stream, EDefaultDevmodeType type,
                                             EPrintTicketScope scope, ULONG *size, PDEVMODEW *dm, BSTR *error)
{
    struct prn_provider *prov = (struct prn_provider *)provider;
    HRESULT hr;
    struct ticket ticket;

    TRACE("%p,%p,%d,%d,%p,%p,%p\n", provider, stream, type, scope, size, dm, error);

    if (!is_valid_provider(provider) || !stream || !size || !dm)
        return E_INVALIDARG;

    hr = initialize_ticket(prov, &ticket);
    if (hr != S_OK) return hr;

    hr = parse_ticket(stream, scope, &ticket);
    if (hr != S_OK) return hr;

    *dm = heap_alloc(sizeof(**dm));
    if (!dm) return E_OUTOFMEMORY;

    ticket_to_devmode(&ticket, *dm);
    *size = sizeof(**dm);

    return S_OK;
}

static HRESULT add_attribute(IXMLDOMElement *element, const WCHAR *attr, const WCHAR *value)
{
    VARIANT var;
    BSTR name;
    HRESULT hr;

    name = SysAllocString(attr);
    V_VT(&var) = VT_BSTR;
    V_BSTR(&var) = SysAllocString(value);

    hr = IXMLDOMElement_setAttribute(element, name, var);

    SysFreeString(name);
    SysFreeString(V_BSTR(&var));

    return hr;
}

static HRESULT create_element(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    IXMLDOMDocument *doc;
    HRESULT hr;

    hr = IXMLDOMElement_get_ownerDocument(root, &doc);
    if (hr != S_OK) return hr;

    hr = IXMLDOMDocument_createElement(doc, (BSTR)name, child);
    if (hr == S_OK)
        hr = IXMLDOMElement_appendChild(root, (IXMLDOMNode *)*child, NULL);

    IXMLDOMDocument_Release(doc);
    return hr;
}

static HRESULT write_int_value(IXMLDOMElement *root, int value)
{
    HRESULT hr;
    IXMLDOMElement *child;
    VARIANT var;

    hr = create_element(root, L"psf:Value", &child);
    if (hr != S_OK) return hr;

    hr = add_attribute(child, L"xsi:type", L"xsd:integer");
    if (hr != S_OK) return hr;

    V_VT(&var) = VT_I4;
    V_I4(&var) = value;
    hr = IXMLDOMElement_put_nodeTypedValue(child, var);

    IXMLDOMElement_Release(child);
    return hr;
}

static HRESULT create_Feature(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    HRESULT hr;

    hr = create_element(root, L"psf:Feature", child);
    if (hr != S_OK) return hr;

    return add_attribute(*child, L"name", name);
}

static HRESULT create_Option(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    HRESULT hr;

    hr = create_element(root, L"psf:Option", child);
    if (hr != S_OK) return hr;

    if (name)
        hr = add_attribute(*child, L"name", name);

    return hr;
}

static HRESULT create_ParameterInit(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    HRESULT hr;

    hr = create_element(root, L"psf:ParameterInit", child);
    if (hr != S_OK) return hr;

    return add_attribute(*child, L"name", name);
}

static HRESULT create_ParameterRef(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    HRESULT hr;

    hr = create_element(root, L"psf:ParameterRef", child);
    if (hr != S_OK) return hr;

    return add_attribute(*child, L"name", name);
}

static HRESULT create_ScoredProperty(IXMLDOMElement *root, const WCHAR *name, IXMLDOMElement **child)
{
    HRESULT hr;

    hr = create_element(root, L"psf:ScoredProperty", child);
    if (hr != S_OK) return hr;

    return add_attribute(*child, L"name", name);
}

static HRESULT write_PageMediaSize(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL, *property;
    HRESULT hr;

    hr = create_Feature(root, L"psk:PageMediaSize", &feature);
    if (hr != S_OK) return hr;

    hr = create_Option(feature, paper_to_media(ticket->page.media.paper), &option);
    if (hr != S_OK) goto fail;

    hr = create_ScoredProperty(option, L"psk:MediaSizeWidth", &property);
    if (hr != S_OK) goto fail;
    hr = write_int_value(property, ticket->page.media.size.width);
    IXMLDOMElement_Release(property);
    if (hr != S_OK) goto fail;

    hr = create_ScoredProperty(option, L"psk:MediaSizeHeight", &property);
    if (hr != S_OK) goto fail;
    hr = write_int_value(property, ticket->page.media.size.height);
    IXMLDOMElement_Release(property);

fail:
    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_PageOutputColor(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL;
    HRESULT hr;

    hr = create_Feature(root, L"psk:PageOutputColor", &feature);
    if (hr != S_OK) return hr;

    if (ticket->page.color == DMCOLOR_COLOR)
        hr = create_Option(feature, L"psk:Color", &option);
    else /* DMCOLOR_MONOCHROME */
        hr = create_Option(feature, L"psk:Monochrome", &option);

    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_PageScaling(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL;
    HRESULT hr;

    hr = create_Feature(root, L"psk:PageScaling", &feature);
    if (hr != S_OK) return hr;

    if (ticket->page.scaling == 100)
    {
        hr = create_Option(feature, L"psk:None", &option);
        if (hr == S_OK) IXMLDOMElement_Release(option);
    }
    else
    {
        hr = create_Option(feature, L"psk:CustomSquare", &option);
        if (hr == S_OK)
        {
            IXMLDOMElement *property, *parameter;

            hr = create_ScoredProperty(option, L"psk:Scale", &property);
            if (hr == S_OK)
            {
                hr = create_ParameterRef(property, L"psk:PageScalingScale", &parameter);
                if (hr == S_OK)
                {
                    IXMLDOMElement_Release(parameter);

                    hr = create_ParameterInit(root, L"psk:PageScalingScale", &parameter);
                    if (hr == S_OK)
                    {
                        hr = write_int_value(parameter, ticket->page.scaling);
                        IXMLDOMElement_Release(parameter);
                    }
                }

                IXMLDOMElement_Release(property);
            }

            IXMLDOMElement_Release(option);
        }
    }

    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_PageResolution(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL, *property;
    HRESULT hr;

    hr = create_Feature(root, L"psk:PageResolution", &feature);
    if (hr != S_OK) return hr;

    hr = create_Option(feature, NULL, &option);
    if (hr != S_OK) goto fail;

    hr = create_ScoredProperty(option, L"psk:ResolutionX", &property);
    if (hr != S_OK) goto fail;
    hr = write_int_value(property, ticket->page.resolution.x);
    IXMLDOMElement_Release(property);
    if (hr != S_OK) goto fail;

    hr = create_ScoredProperty(option, L"psk:ResolutionY", &property);
    if (hr != S_OK) goto fail;
    hr = write_int_value(property, ticket->page.resolution.y);
    IXMLDOMElement_Release(property);

fail:
    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_PageOrientation(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL;
    HRESULT hr;

    hr = create_Feature(root, L"psk:PageOrientation", &feature);
    if (hr != S_OK) return hr;

    if (ticket->page.orientation == DMORIENT_PORTRAIT)
        hr = create_Option(feature, L"psk:Portrait", &option);
    else /* DMORIENT_LANDSCAPE */
        hr = create_Option(feature, L"psk:Landscape", &option);

    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_DocumentCollate(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL;
    HRESULT hr;

    hr = create_Feature(root, L"psk:DocumentCollate", &feature);
    if (hr != S_OK) return hr;

    if (ticket->document.collate == DMCOLLATE_TRUE)
        hr = create_Option(feature, L"psk:Collated", &option);
    else /* DMCOLLATE_FALSE */
        hr = create_Option(feature, L"psk:Uncollated", &option);

    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_JobInputBin(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *feature, *option = NULL;
    HRESULT hr;

    hr = create_Feature(root, L"psk:JobInputBin", &feature);
    if (hr != S_OK) return hr;

    if (ticket->job.input_bin != DMBIN_AUTO)
        FIXME("job.input_bin: %d\n", ticket->job.input_bin);

    hr = create_Option(feature, L"psk:AutoSelect", &option);

    if (option) IXMLDOMElement_Release(option);
    IXMLDOMElement_Release(feature);
    return hr;
}

static HRESULT write_JobCopies(IXMLDOMElement *root, const struct ticket *ticket)
{
    IXMLDOMElement *parameter;
    HRESULT hr;

    hr = create_ParameterInit(root, L"psk:JobCopiesAllDocuments", &parameter);
    if (hr != S_OK) return hr;

    hr = write_int_value(parameter, ticket->job.copies);

    IXMLDOMElement_Release(parameter);
    return hr;
}

static HRESULT write_attributes(IXMLDOMElement *element)
{
    HRESULT hr;

    hr = add_attribute(element, L"xmlns:psf", L"http://schemas.microsoft.com/windows/2003/08/printing/printschemaframework");
    if (hr != S_OK) return hr;
    hr = add_attribute(element, L"xmlns:psk", L"http://schemas.microsoft.com/windows/2003/08/printing/printschemakeywords");
    if (hr != S_OK) return hr;
    hr = add_attribute(element, L"xmlns:xsi", L"http://www.w3.org/2001/XMLSchema-instance");
    if (hr != S_OK) return hr;
    hr = add_attribute(element, L"xmlns:xsd", L"http://www.w3.org/2001/XMLSchema");
    if (hr != S_OK) return hr;

    return add_attribute(element, L"version", L"1");
}

static HRESULT write_ticket(IStream *stream, const struct ticket *ticket, EPrintTicketScope scope)
{
    static const char xmldecl[] = "<?xml version=\"1.0\" encoding=\"UTF-8\"?>\r\n";
    HRESULT hr;
    IXMLDOMDocument *doc;
    IXMLDOMElement *root = NULL;
    VARIANT var;

    hr = CoCreateInstance(&CLSID_DOMDocument, NULL, CLSCTX_INPROC_SERVER,
                          &IID_IXMLDOMDocument, (void **)&doc);
    if (hr != S_OK) return hr;

    hr = IXMLDOMDocument_createElement(doc, (BSTR)L"psf:PrintTicket", &root);
    if (hr != S_OK) goto fail;

    hr = IXMLDOMDocument_appendChild(doc, (IXMLDOMNode *)root, NULL);
    if (hr != S_OK) goto fail;

    hr = write_attributes(root);
    if (hr != S_OK) goto fail;

    hr = write_PageMediaSize(root, ticket);
    if (hr != S_OK) goto fail;
    hr = write_PageOutputColor(root, ticket);
    if (hr != S_OK) goto fail;
    hr = write_PageScaling(root, ticket);
    if (hr != S_OK) goto fail;
    hr = write_PageResolution(root, ticket);
    if (hr != S_OK) goto fail;
    hr = write_PageOrientation(root, ticket);
    if (hr != S_OK) goto fail;

    if (scope >= kPTDocumentScope)
    {
        hr = write_DocumentCollate(root, ticket);
        if (hr != S_OK) goto fail;
    }

    if (scope >= kPTJobScope)
    {
        hr = write_JobInputBin(root, ticket);
        if (hr != S_OK) goto fail;
        hr = write_JobCopies(root, ticket);
        if (hr != S_OK) goto fail;
    }

    hr = IStream_Write(stream, xmldecl, strlen(xmldecl), NULL);
    if (hr != S_OK) goto fail;

    V_VT(&var) = VT_UNKNOWN;
    V_UNKNOWN(&var) = (IUnknown *)stream;
    hr = IXMLDOMDocument_save(doc, var);

fail:
    if (root) IXMLDOMElement_Release(root);
    IXMLDOMDocument_Release(doc);
    return hr;
}

static void dump_fields(DWORD fields)
{
    int add_space = 0;

#define CHECK_FIELD(flag) \
do \
{ \
    if (fields & flag) \
    { \
        if (add_space++) TRACE(" "); \
        TRACE(#flag); \
        fields &= ~flag; \
    } \
} \
while (0)

    CHECK_FIELD(DM_ORIENTATION);
    CHECK_FIELD(DM_PAPERSIZE);
    CHECK_FIELD(DM_PAPERLENGTH);
    CHECK_FIELD(DM_PAPERWIDTH);
    CHECK_FIELD(DM_SCALE);
    CHECK_FIELD(DM_POSITION);
    CHECK_FIELD(DM_NUP);
    CHECK_FIELD(DM_DISPLAYORIENTATION);
    CHECK_FIELD(DM_COPIES);
    CHECK_FIELD(DM_DEFAULTSOURCE);
    CHECK_FIELD(DM_PRINTQUALITY);
    CHECK_FIELD(DM_COLOR);
    CHECK_FIELD(DM_DUPLEX);
    CHECK_FIELD(DM_YRESOLUTION);
    CHECK_FIELD(DM_TTOPTION);
    CHECK_FIELD(DM_COLLATE);
    CHECK_FIELD(DM_FORMNAME);
    CHECK_FIELD(DM_LOGPIXELS);
    CHECK_FIELD(DM_BITSPERPEL);
    CHECK_FIELD(DM_PELSWIDTH);
    CHECK_FIELD(DM_PELSHEIGHT);
    CHECK_FIELD(DM_DISPLAYFLAGS);
    CHECK_FIELD(DM_DISPLAYFREQUENCY);
    CHECK_FIELD(DM_ICMMETHOD);
    CHECK_FIELD(DM_ICMINTENT);
    CHECK_FIELD(DM_MEDIATYPE);
    CHECK_FIELD(DM_DITHERTYPE);
    CHECK_FIELD(DM_PANNINGWIDTH);
    CHECK_FIELD(DM_PANNINGHEIGHT);
    if (fields) TRACE(" %#x", fields);
    TRACE("\n");
#undef CHECK_FIELD
}

/* Dump DEVMODE structure without a device specific part.
 * Some applications and drivers fail to specify correct field
 * flags (like DM_FORMNAME), so dump everything.
 */
static void dump_devmode(const DEVMODEW *dm)
{
    if (!TRACE_ON(prntvpt)) return;

    TRACE("dmDeviceName: %s\n", debugstr_w(dm->dmDeviceName));
    TRACE("dmSpecVersion: 0x%04x\n", dm->dmSpecVersion);
    TRACE("dmDriverVersion: 0x%04x\n", dm->dmDriverVersion);
    TRACE("dmSize: 0x%04x\n", dm->dmSize);
    TRACE("dmDriverExtra: 0x%04x\n", dm->dmDriverExtra);
    TRACE("dmFields: 0x%04x\n", dm->dmFields);
    dump_fields(dm->dmFields);
    TRACE("dmOrientation: %d\n", dm->u1.s1.dmOrientation);
    TRACE("dmPaperSize: %d\n", dm->u1.s1.dmPaperSize);
    TRACE("dmPaperLength: %d\n", dm->u1.s1.dmPaperLength);
    TRACE("dmPaperWidth: %d\n", dm->u1.s1.dmPaperWidth);
    TRACE("dmScale: %d\n", dm->u1.s1.dmScale);
    TRACE("dmCopies: %d\n", dm->u1.s1.dmCopies);
    TRACE("dmDefaultSource: %d\n", dm->u1.s1.dmDefaultSource);
    TRACE("dmPrintQuality: %d\n", dm->u1.s1.dmPrintQuality);
    TRACE("dmColor: %d\n", dm->dmColor);
    TRACE("dmDuplex: %d\n", dm->dmDuplex);
    TRACE("dmYResolution: %d\n", dm->dmYResolution);
    TRACE("dmTTOption: %d\n", dm->dmTTOption);
    TRACE("dmCollate: %d\n", dm->dmCollate);
    TRACE("dmFormName: %s\n", debugstr_w(dm->dmFormName));
    TRACE("dmLogPixels %u\n", dm->dmLogPixels);
    TRACE("dmBitsPerPel %u\n", dm->dmBitsPerPel);
    TRACE("dmPelsWidth %u\n", dm->dmPelsWidth);
    TRACE("dmPelsHeight %u\n", dm->dmPelsHeight);
}

HRESULT WINAPI PTConvertDevModeToPrintTicket(HPTPROVIDER provider, ULONG size, PDEVMODEW dm,
                                             EPrintTicketScope scope, IStream *stream)
{
    struct prn_provider *prov = (struct prn_provider *)provider;
    struct ticket ticket;
    HRESULT hr;

    TRACE("%p,%u,%p,%d,%p\n", provider, size, dm, scope, stream);

    if (!is_valid_provider(provider) || !dm || !stream)
        return E_INVALIDARG;

    dump_devmode(dm);

    if (!IsValidDevmodeW(dm, size))
        return E_INVALIDARG;

    hr = initialize_ticket(prov, &ticket);
    if (hr != S_OK) return hr;
    devmode_to_ticket(dm, &ticket);

    return write_ticket(stream, &ticket, scope);
}
