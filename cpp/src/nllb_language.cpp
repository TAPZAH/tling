#include "offline_translator/nllb_language.hpp"

#include <map>
#include <stdexcept>

namespace offline_translator {

std::string nllb_language_code(std::string_view language_code) {
    if (language_code.find('_') != std::string_view::npos) {
        return std::string(language_code);
    }

    static const std::map<std::string, std::string> codes{
        {"en", "eng_Latn"},
        {"ru", "rus_Cyrl"},
        {"de", "deu_Latn"},
        {"fr", "fra_Latn"},
        {"es", "spa_Latn"},
        {"it", "ita_Latn"},
        {"pt", "por_Latn"},
        {"zh", "zho_Hans"},
        {"ja", "jpn_Jpan"},
        {"ko", "kor_Hang"},
        {"ar", "arb_Arab"},
        {"uk", "ukr_Cyrl"},
        {"pl", "pol_Latn"},
        {"tr", "tur_Latn"},
        {"nl", "nld_Latn"},
        {"cs", "ces_Latn"},
        {"sv", "swe_Latn"},
        {"fi", "fin_Latn"},
        {"el", "ell_Grek"},
        {"he", "heb_Hebr"},
        {"hi", "hin_Deva"},
        {"id", "ind_Latn"},
        {"az", "azj_Latn"},
        {"be", "bel_Cyrl"},
        {"bg", "bul_Cyrl"},
        {"bn", "ben_Beng"},
        {"bs", "bos_Latn"},
        {"ca", "cat_Latn"},
        {"da", "dan_Latn"},
        {"et", "est_Latn"},
        {"fa", "pes_Arab"},
        {"gu", "guj_Gujr"},
        {"hr", "hrv_Latn"},
        {"hu", "hun_Latn"},
        {"is", "isl_Latn"},
        {"kn", "kan_Knda"},
        {"lt", "lit_Latn"},
        {"lv", "lvs_Latn"},
        {"ml", "mal_Mlym"},
        {"ms", "zsm_Latn"},
        {"mt", "mlt_Latn"},
        {"nb", "nob_Latn"},
        {"nn", "nno_Latn"},
        {"ro", "ron_Latn"},
        {"sk", "slk_Latn"},
        {"sl", "slv_Latn"},
        {"sq", "als_Latn"},
        {"sr", "srp_Cyrl"},
        {"ta", "tam_Taml"},
        {"te", "tel_Telu"},
        {"th", "tha_Thai"},
        {"vi", "vie_Latn"},
        {"mk", "mkd_Cyrl"},
        {"gl", "glg_Latn"},
        {"ur", "urd_Arab"},
        {"ka", "kat_Geor"},
    };
    const auto found = codes.find(std::string(language_code));
    if (found == codes.end()) {
        throw std::runtime_error(
            "NLLB не знает язык: " + std::string(language_code));
    }
    return found->second;
}

}  // пространство имён offline_translator
