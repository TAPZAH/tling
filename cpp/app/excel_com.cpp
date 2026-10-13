#include "excel_com.hpp"

#include "offline_translator/excel_cells.hpp"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <objbase.h>
#include <oleauto.h>
#include <tlhelp32.h>

namespace tling_excel {
namespace {

// Владение COM-указателем: Release в деструкторе.
class Dispatch {
public:
    Dispatch() = default;
    explicit Dispatch(IDispatch* value) : value_(value) {}
    ~Dispatch() {
        reset();
    }
    Dispatch(const Dispatch&) = delete;
    Dispatch& operator=(const Dispatch&) = delete;
    Dispatch(Dispatch&& other) noexcept : value_(other.value_) {
        other.value_ = nullptr;
    }
    Dispatch& operator=(Dispatch&& other) noexcept {
        if (this != &other) {
            reset(other.value_);
            other.value_ = nullptr;
        }
        return *this;
    }

    IDispatch* get() const {
        return value_;
    }
    IDispatch* operator->() const {
        return value_;
    }
    explicit operator bool() const {
        return value_ != nullptr;
    }
    void reset(IDispatch* value = nullptr) {
        if (value_) {
            value_->Release();
        }
        value_ = value;
    }

private:
    IDispatch* value_{nullptr};
};

// Забирает IDispatch из VARIANT, обнуляя его, чтобы VariantClear не
// освободил указатель повторно.
IDispatch* take_dispatch(VARIANT* value) {
    if (value->vt != VT_DISPATCH) {
        VariantClear(value);
        return nullptr;
    }
    IDispatch* result = value->pdispVal;
    value->pdispVal = nullptr;
    VariantClear(value);
    return result;
}

bool get_property(IDispatch* object, const wchar_t* name, VARIANT* result) {
    if (!object) {
        return false;
    }
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(name);
    if (FAILED(object->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    VariantInit(result);
    DISPPARAMS params{};
    return SUCCEEDED(object->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYGET,
        &params,
        result,
        nullptr,
        nullptr));
}

// Range.Item(row, col). В DISPPARAMS аргументы идут в обратном порядке.
bool call_item(IDispatch* range, long row, long col, VARIANT* result) {
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(L"Item");
    if (FAILED(range->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    VARIANT args[2];
    VariantInit(&args[0]);
    args[0].vt = VT_I4;
    args[0].lVal = col;
    VariantInit(&args[1]);
    args[1].vt = VT_I4;
    args[1].lVal = row;
    DISPPARAMS params{args, nullptr, 2, 0};
    VariantInit(result);
    const HRESULT status = range->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYGET | DISPATCH_METHOD,
        &params,
        result,
        nullptr,
        nullptr);
    VariantClear(&args[0]);
    VariantClear(&args[1]);
    return SUCCEEDED(status);
}

bool put_value2(IDispatch* cell, const std::wstring& text) {
    if (!cell) {
        return false;
    }
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(L"Value2");
    if (FAILED(cell->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    VARIANT value;
    VariantInit(&value);
    value.vt = VT_BSTR;
    value.bstrVal = SysAllocStringLen(
        text.data(),
        static_cast<UINT>(text.size()));
    if (!value.bstrVal) {
        return false;
    }
    DISPID put_id = DISPID_PROPERTYPUT;
    DISPPARAMS params{&value, &put_id, 1, 1};
    const HRESULT status = cell->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYPUT,
        &params,
        nullptr,
        nullptr,
        nullptr);
    VariantClear(&value);
    return SUCCEEDED(status);
}

bool has_formula(IDispatch* range, long row, long col, bool* formula) {    VARIANT item;
    if (!call_item(range, row, col, &item)) {
        return false;
    }
    Dispatch cell(take_dispatch(&item));
    if (!cell) {
        return false;
    }
    VARIANT value;
    if (!get_property(cell.get(), L"HasFormula", &value)) {
        return false;
    }
    if (value.vt == VT_BOOL) {
        *formula = value.boolVal == VARIANT_TRUE;
    }
    VariantClear(&value);
    return true;
}

std::wstring variant_text(const VARIANT& value) {
    if (value.vt == VT_BSTR && value.bstrVal) {
        return std::wstring(value.bstrVal, SysStringLen(value.bstrVal));
    }
    return {};
}

bool put_property(IDispatch* object, const wchar_t* name, VARIANT* value) {
    if (!object) {
        return false;
    }
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(name);
    if (FAILED(object->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    DISPID put_id = DISPID_PROPERTYPUT;
    DISPPARAMS params{value, &put_id, 1, 1};
    return SUCCEEDED(object->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYPUT,
        &params,
        nullptr,
        nullptr,
        nullptr));
}

bool call_method(IDispatch* object, const wchar_t* name, VARIANT* result) {
    if (!object) {
        return false;
    }
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(name);
    if (FAILED(object->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    VariantInit(result);
    DISPPARAMS params{};
    return SUCCEEDED(object->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_METHOD,
        &params,
        result,
        nullptr,
        nullptr));
}

bool call_range(
    IDispatch* sheet,
    const std::wstring& first,
    const std::wstring& second,
    VARIANT* result) {
    if (!sheet) {
        return false;
    }
    DISPID id = 0;
    LPOLESTR wide = const_cast<LPOLESTR>(L"Range");
    if (FAILED(sheet->GetIDsOfNames(
            IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
        return false;
    }
    // DISPPARAMS хранит аргументы в обратном порядке.
    VARIANT args[2];
    VariantInit(&args[0]);
    args[0].vt = VT_BSTR;
    args[0].bstrVal = SysAllocString(second.c_str());
    VariantInit(&args[1]);
    args[1].vt = VT_BSTR;
    args[1].bstrVal = SysAllocString(first.c_str());
    DISPPARAMS params{args, nullptr, 2, 0};
    VariantInit(result);
    const HRESULT status = sheet->Invoke(
        id,
        IID_NULL,
        LOCALE_USER_DEFAULT,
        DISPATCH_PROPERTYGET | DISPATCH_METHOD,
        &params,
        result,
        nullptr,
        nullptr);
    VariantClear(&args[0]);
    VariantClear(&args[1]);
    return SUCCEEDED(status);
}

bool any_excel_process() {
    const HANDLE snapshot = CreateToolhelp32Snapshot(TH32CS_SNAPPROCESS, 0);
    if (snapshot == INVALID_HANDLE_VALUE) {
        return false;
    }
    PROCESSENTRY32W entry{};
    entry.dwSize = sizeof(entry);
    bool found = false;
    if (Process32FirstW(snapshot, &entry)) {
        do {
            if (_wcsicmp(entry.szExeFile, L"EXCEL.EXE") == 0) {
                found = true;
                break;
            }
        } while (Process32NextW(snapshot, &entry));
    }
    CloseHandle(snapshot);
    return found;
}

}  // анонимное пространство имён

struct ExcelSession::Impl {
    Dispatch app;
    Dispatch range;
    bool com_initialized{false};
};

ExcelSession::ExcelSession() : impl_(new Impl()) {}

ExcelSession::~ExcelSession() {
    if (impl_) {
        impl_->range.reset();
        impl_->app.reset();
        if (impl_->com_initialized) {
            CoUninitialize();
        }
        delete impl_;
    }
}

bool ExcelSession::attach(std::wstring* error) {
    if (!impl_) {
        return false;
    }
    if (!impl_->com_initialized) {
        const HRESULT status =
            CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
        if (FAILED(status)) {
            if (error) {
                *error = L"COM недоступен";
            }
            return false;
        }
        impl_->com_initialized = true;
    }
    CLSID clsid{};
    if (FAILED(CLSIDFromProgID(L"Excel.Application", &clsid))) {
        if (error) {
            *error = L"Excel не установлен";
        }
        return false;
    }
    IUnknown* unknown = nullptr;
    if (FAILED(GetActiveObject(clsid, nullptr, &unknown)) || !unknown) {
        if (error) {
            *error = L"Excel не запущен";
        }
        return false;
    }
    IDispatch* app = nullptr;
    const HRESULT status =
        unknown->QueryInterface(IID_IDispatch, reinterpret_cast<void**>(&app));
    unknown->Release();
    if (FAILED(status) || !app) {
        if (error) {
            *error = L"Excel не отвечает";
        }
        return false;
    }
    impl_->app.reset(app);
    return true;
}

bool ExcelSession::read_selection(
    SelectionData* data,
    std::wstring* error) {
    if (!impl_ || !impl_->app) {
        if (error) {
            *error = L"Excel не подключён";
        }
        return false;
    }
    impl_->range.reset();
    VARIANT selection;
    if (!get_property(impl_->app.get(), L"Selection", &selection)) {
        if (error) {
            *error = L"не удалось получить выделение";
        }
        return false;
    }
    impl_->range.reset(take_dispatch(&selection));
    if (!impl_->range) {
        if (error) {
            *error = L"выделен не диапазон ячеек";
        }
        return false;
    }
    IDispatch* range = impl_->range.get();

    // Несколько несвязанных диапазонов (Ctrl+выделение) не поддерживаем.
    VARIANT areas;
    if (get_property(range, L"Areas", &areas)) {
        Dispatch areas_dispatch(take_dispatch(&areas));
        if (areas_dispatch) {
            VARIANT count;
            if (get_property(areas_dispatch.get(), L"Count", &count)) {
                const bool many = count.vt == VT_I4 && count.lVal > 1;
                VariantClear(&count);
                if (many) {
                    if (error) {
                        *error = L"выделено несколько диапазонов";
                    }
                    return false;
                }
            }
        }
    }

    // Защита от «выделить весь столбец»: сначала считаем ячейки.
    VARIANT count;
    if (get_property(range, L"Count", &count)) {
        const bool too_many = count.vt == VT_I4 &&
            count.lVal > static_cast<long>(
                offline_translator::kExcelMaxCells);
        VariantClear(&count);
        if (too_many) {
            if (error) {
                *error = L"слишком большой диапазон";
            }
            return false;
        }
    }

    // Формулы: FALSE — нигде, TRUE — везде, иначе проверим по ячейкам.
    int formulas_all = -1;
    VARIANT formula_flag;
    if (get_property(range, L"HasFormula", &formula_flag)) {
        if (formula_flag.vt == VT_BOOL) {
            formulas_all = formula_flag.boolVal == VARIANT_TRUE ? 1 : 0;
        }
        VariantClear(&formula_flag);
    }

    VARIANT values;
    if (!get_property(range, L"Value2", &values)) {
        if (error) {
            *error = L"не удалось прочитать значения";
        }
        return false;
    }
    data->rows = 1;
    data->cols = 1;
    if ((values.vt & VT_ARRAY) != 0 && values.parray) {
        SAFEARRAY* array = values.parray;
        long lower_row = 0;
        long upper_row = 0;
        long lower_col = 0;
        long upper_col = 0;
        if (FAILED(SafeArrayGetLBound(array, 1, &lower_row)) ||
            FAILED(SafeArrayGetUBound(array, 1, &upper_row)) ||
            FAILED(SafeArrayGetLBound(array, 2, &lower_col)) ||
            FAILED(SafeArrayGetUBound(array, 2, &upper_col))) {
            VariantClear(&values);
            if (error) {
                *error = L"не удалось разобрать диапазон";
            }
            return false;
        }
        data->rows = upper_row - lower_row + 1;
        data->cols = upper_col - lower_col + 1;
        const std::size_t total = static_cast<std::size_t>(data->rows) *
            static_cast<std::size_t>(data->cols);
        data->texts.assign(total, std::wstring{});
        data->formulas.assign(total, formulas_all == 1);
        for (long row = 0; row < data->rows; ++row) {
            for (long col = 0; col < data->cols; ++col) {
                const long indices[2] = {
                    lower_row + row,
                    lower_col + col};
                VARIANT cell;
                VariantInit(&cell);
                if (FAILED(SafeArrayGetElement(
                        array,
                        const_cast<long*>(indices),
                        &cell))) {
                    continue;
                }
                const std::wstring text = variant_text(cell);
                VariantClear(&cell);
                const std::size_t index =
                    static_cast<std::size_t>(row) *
                        static_cast<std::size_t>(data->cols) +
                    static_cast<std::size_t>(col);
                data->texts[index] = text;
                if (!text.empty() && formulas_all == -1) {
                    // Неизвестно — считаем формулой, чтобы не затереть.
                    bool formula = true;
                    if (has_formula(range, row + 1, col + 1, &formula)) {
                        data->formulas[index] = formula;
                    }
                }
            }
        }
    } else {
        const std::wstring text = variant_text(values);
        data->texts.assign(1, text);
        data->formulas.assign(1, formulas_all == 1);
        if (!text.empty() && formulas_all == -1) {
            bool formula = true;
            if (has_formula(range, 1, 1, &formula)) {
                data->formulas[0] = formula;
            }
        }
    }
    VariantClear(&values);
    return true;
}

bool ExcelSession::write_cell(
    long row,
    long col,
    const std::wstring& text,
    std::wstring* error) {
    if (!impl_ || !impl_->range) {
        if (error) {
            *error = L"Excel не подключён";
        }
        return false;
    }
    VARIANT item;
    if (!call_item(impl_->range.get(), row, col, &item)) {
        if (error) {
            *error = L"ячейка недоступна";
        }
        return false;
    }
    Dispatch cell(take_dispatch(&item));
    if (!cell) {
        if (error) {
            *error = L"ячейка недоступна";
        }
        return false;
    }
    if (!put_value2(cell.get(), text)) {
        if (error) {
            *error = L"не удалось записать значение";
        }
        return false;
    }
    return true;
}

ExcelSelfTestResult excel_com_self_test(std::wstring* message) {
    CLSID clsid{};
    if (FAILED(CLSIDFromProgID(L"Excel.Application", &clsid))) {
        if (message) {
            *message = L"Excel не установлен";
        }
        return ExcelSelfTestResult::skipped;
    }
    // Чужую сессию не трогаем: если Excel где-то запущен (даже скрытый),
    // самотест пропускается.
    if (any_excel_process()) {
        if (message) {
            *message = L"Excel уже запущен — самотест пропущен";
        }
        return ExcelSelfTestResult::skipped;
    }
    IUnknown* running = nullptr;
    if (SUCCEEDED(GetActiveObject(clsid, nullptr, &running)) && running) {
        running->Release();
        if (message) {
            *message = L"Excel уже запущен — самотест пропущен";
        }
        return ExcelSelfTestResult::skipped;
    }
    if (FAILED(CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED))) {
        if (message) {
            *message = L"COM не инициализируется";
        }
        return ExcelSelfTestResult::failed;
    }
    struct Uninit {
        ~Uninit() {
            CoUninitialize();
        }
    } uninit;
    IDispatch* raw_app = nullptr;
    if (FAILED(CoCreateInstance(
            clsid,
            nullptr,
            CLSCTX_LOCAL_SERVER,
            IID_IDispatch,
            reinterpret_cast<void**>(&raw_app))) ||
        !raw_app) {
        if (message) {
            *message = L"не удалось создать Excel";
        }
        return ExcelSelfTestResult::skipped;
    }
    Dispatch app(raw_app);
    // Закрываем только свою книгу и свой экземпляр Excel.
    struct QuitGuard {
        IDispatch* app{nullptr};
        IDispatch* workbook{nullptr};
        ~QuitGuard() {
            if (workbook) {
                VARIANT save;
                VariantInit(&save);
                save.vt = VT_BOOL;
                save.boolVal = VARIANT_FALSE;
                DISPID id = 0;
                LPOLESTR wide = const_cast<LPOLESTR>(L"Close");
                if (SUCCEEDED(workbook->GetIDsOfNames(
                        IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
                    DISPPARAMS params{&save, nullptr, 1, 0};
                    workbook->Invoke(
                        id,
                        IID_NULL,
                        LOCALE_USER_DEFAULT,
                        DISPATCH_METHOD,
                        &params,
                        nullptr,
                        nullptr,
                        nullptr);
                }
                VariantClear(&save);
            }
            if (app) {
                VARIANT quit;
                VariantInit(&quit);
                call_method(app, L"Quit", &quit);
                VariantClear(&quit);
            }
        }
    } quit_guard;
    quit_guard.app = app.get();
    const auto fail = [message](const wchar_t* reason) {
        if (message) {
            *message = reason;
        }
        return ExcelSelfTestResult::failed;
    };

    {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BOOL;
        value.boolVal = VARIANT_FALSE;
        put_property(app.get(), L"DisplayAlerts", &value);
        VariantClear(&value);
    }
    Dispatch workbook;
    {
        VARIANT workbooks;
        if (!get_property(app.get(), L"Workbooks", &workbooks)) {
            return fail(L"нет коллекции Workbooks");
        }
        Dispatch books(take_dispatch(&workbooks));
        VARIANT added;
        if (!books || !call_method(books.get(), L"Add", &added)) {
            return fail(L"не удалось добавить книгу");
        }
        workbook.reset(take_dispatch(&added));
        quit_guard.workbook = workbook.get();
        if (!workbook) {
            return fail(L"Excel не вернул книгу");
        }
    }
    // Берём лист именно своей книги: при автовосстановлении активной может
    // оказаться чужая книга.
    Dispatch sheet;
    {
        VARIANT worksheets;
        if (!get_property(workbook.get(), L"Worksheets", &worksheets)) {
            return fail(L"нет коллекции листов");
        }
        Dispatch sheets(take_dispatch(&worksheets));
        DISPID id = 0;
        LPOLESTR wide = const_cast<LPOLESTR>(L"Item");
        if (!sheets ||
            FAILED(sheets->GetIDsOfNames(
                IID_NULL, &wide, 1, LOCALE_USER_DEFAULT, &id))) {
            return fail(L"нет первого листа");
        }
        VARIANT arg;
        VariantInit(&arg);
        arg.vt = VT_I4;
        arg.lVal = 1;
        VARIANT item;
        VariantInit(&item);
        DISPPARAMS params{&arg, nullptr, 1, 0};
        const HRESULT status = sheets->Invoke(
            id,
            IID_NULL,
            LOCALE_USER_DEFAULT,
            DISPATCH_PROPERTYGET | DISPATCH_METHOD,
            &params,
            &item,
            nullptr,
            nullptr);
        VariantClear(&arg);
        if (FAILED(status)) {
            return fail(L"нет первого листа");
        }
        sheet.reset(take_dispatch(&item));
    }
    {
        VARIANT value;
        VariantInit(&value);
        value.vt = VT_BOOL;
        value.boolVal = VARIANT_TRUE;
        put_property(app.get(), L"Visible", &value);
        VariantClear(&value);
    }
    {
        VARIANT value;
        if (!sheet || !call_method(sheet.get(), L"Activate", &value)) {
            return fail(L"не удалось активировать лист");
        }
        VariantClear(&value);
    }
    Dispatch range;
    {
        VARIANT value;
        if (!sheet ||
            !call_range(sheet.get(), L"A1", L"B2", &value)) {
            return fail(L"не удалось получить диапазон");
        }
        range.reset(take_dispatch(&value));
    }
    const wchar_t* const values[4] = {
        L"Hello",
        L"World",
        L"",
        L"Table"};
    for (int index = 0; index < 4; ++index) {
        VARIANT item;
        if (!range ||
            !call_item(
                range.get(),
                index / 2 + 1,
                index % 2 + 1,
                &item)) {
            return fail(L"ячейка A1:B2 недоступна");
        }
        Dispatch cell(take_dispatch(&item));
        if (!cell || !put_value2(cell.get(), values[index])) {
            return fail(L"не удалось заполнить ячейки");
        }
    }
    {
        VARIANT value;
        if (!range || !call_method(range.get(), L"Select", &value)) {
            return fail(L"не удалось выделить диапазон");
        }
        VariantClear(&value);
    }

    ExcelSession session;
    std::wstring error;
    if (!session.attach(&error)) {
        return fail(L"attach не сработал");
    }
    SelectionData data;
    if (!session.read_selection(&data, &error)) {
        return fail(L"read_selection не сработал");
    }
    if (data.rows != 2 || data.cols != 2 || data.texts.size() != 4) {
        return fail(L"неверный размер выделения");
    }
    if (data.texts[0] != L"Hello" || data.texts[1] != L"World" ||
        !data.texts[2].empty() || data.texts[3] != L"Table") {
        return fail(L"неверные тексты ячеек");
    }
    if (data.formulas[0] || data.formulas[3]) {
        return fail(L"обычные ячейки помечены формулами");
    }
    if (!session.write_cell(1, 1, L"Hello / Привет", &error)) {
        return fail(L"write_cell не сработал");
    }
    {
        VARIANT item;
        if (!range || !call_item(range.get(), 1, 1, &item)) {
            return fail(L"не удалось перечитать ячейку");
        }
        Dispatch cell(take_dispatch(&item));
        VARIANT value;
        if (!cell || !get_property(cell.get(), L"Value2", &value)) {
            return fail(L"не удалось прочитать A1");
        }
        const std::wstring text = variant_text(value);
        VariantClear(&value);
        if (text != L"Hello / Привет") {
            return fail(L"записанный текст не совпал");
        }
    }
    if (message) {
        *message = L"COM: чтение и запись выделения работают";
    }
    return ExcelSelfTestResult::passed;
}

}  // namespace tling_excel
