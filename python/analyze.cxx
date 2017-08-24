#include <boost/python.hpp>

#include "wrap_obj.hxx"
#include "analyze.hxx"
#include "sssr.hxx"
#include "mol2.hxx"
#include "elements.hxx"
#include "smarts.hxx"

using namespace desres::msys;
using namespace boost::python;

namespace {
    void assign_1(SystemPtr mol, bool compute_resonant_charges) {
        unsigned flags = 0;
        if (compute_resonant_charges) flags |= AssignBondOrder::ComputeResonantCharges;
        AssignBondOrderAndFormalCharge(mol, flags);
    }
    void assign_2(SystemPtr mol, list ids, bool compute_resonant_charges) {
        unsigned flags = 0;
        if (compute_resonant_charges) flags |= AssignBondOrder::ComputeResonantCharges;
        AssignBondOrderAndFormalCharge(mol, ids_from_python(ids), INT_MAX, flags);
    }
    void assign_3(SystemPtr mol, list ids, int total_charge, bool compute_resonant_charges) {
        unsigned flags = 0;
        if (compute_resonant_charges) flags |= AssignBondOrder::ComputeResonantCharges;
        AssignBondOrderAndFormalCharge(mol, ids_from_python(ids), total_charge, flags);
    }

    list find_distinct_fragments(SystemPtr mol) {
        MultiIdList fragments;
        mol->updateFragids(&fragments);
        return to_python(FindDistinctFragments(mol, fragments));
    }

    list find_matches(SmartsPattern const& s, AnnotatedSystem const& sys, 
                                              list const& starts) {
        return to_python(s.findMatches(sys, ids_from_python(starts)));
    }

    list wrap_sssr(SystemPtr mol, list atoms, bool all_relevant=false)
    {
        return to_python(GetSSSR(mol, ids_from_python(atoms), all_relevant));
    }
    list ring_systems(SystemPtr mol, list atoms) {
        return to_python(RingSystems(mol, GetSSSR(mol, ids_from_python(atoms), true)));
    }
    double elec_for_element(int n) {
        return DataForElement(n).eneg;
    }

    list compute_topids(SystemPtr mol) {
        return to_python(ComputeTopologicalIds(mol));
    }

    void guess_hydrogen(SystemPtr mol, list ids) {
        GuessHydrogenPositions(mol, ids_from_python(ids));
    }

}

namespace desres { namespace msys { 

    void export_analyze() {
        def("AssignBondOrderAndFormalCharge", assign_1);
        def("AssignBondOrderAndFormalCharge", assign_2);
        def("AssignBondOrderAndFormalCharge", assign_3);
        /* Yes, we have two interfaces for SSSR, this one and the one in
         * AnnotatedSystem.  This one lets you specify which atoms you
         * want the rings for, and doesn't force you to do any annotation, 
         * which is what we want.  AnnotatedSystem's rings() method only
         * lets you find rings connected to specific atoms or bonds. 
         */
        def("GetSSSR", wrap_sssr);
        def("RingSystems", ring_systems);
        def("ComputeTopologicalIds", compute_topids);
        def("GuessBondConnectivity", GuessBondConnectivity);
        def("FindDistinctFragments", find_distinct_fragments);
        def("RadiusForElement", RadiusForElement);
        def("MassForElement", MassForElement);
        def("PeriodForElement", PeriodForElement);
        def("GroupForElement", GroupForElement);
        def("ElementForAbbreviation", ElementForAbbreviation);
        def("GuessHydrogenPositions", guess_hydrogen);
        def("AbbreviationForElement", AbbreviationForElement);
        def("Analyze", Analyze);
        def("GuessAtomicNumber", GuessAtomicNumber);
        def("ElectronegativityForElement", elec_for_element, "Allen-scale electronegativity");

        class_<SmartsPattern>("SmartsPattern", init<std::string const&>())
            .def("atomCount", &SmartsPattern::atomCount)
            .def("pattern",   &SmartsPattern::pattern,
                    return_value_policy<copy_const_reference>())
            .def("warnings",  &SmartsPattern::warnings,
                    return_value_policy<copy_const_reference>())
            .def("findMatches",     &find_matches)
            .def("match",     &SmartsPattern::match)
            ;
    }
}}

